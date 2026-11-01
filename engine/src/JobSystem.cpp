#include "engine/JobSystem.h"
#include "engine/Profile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>

namespace engine {

JobSystem::JobSystem(unsigned threads)
{
    if (threads == 0) {
        const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        threads = std::max(1u, hw - 1); // the main thread works too
    }
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i)
        workers_.emplace_back([this, i] {
            const std::string name = "Job worker " + std::to_string(i + 1);
            ENGINE_THREAD_NAME(name.c_str());
            (void)name;
            workerLoop();
        });
}

JobSystem::~JobSystem()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : workers_)
        t.join();
}

JobSystem& JobSystem::global()
{
    static JobSystem instance;
    return instance;
}

void JobSystem::push(std::function<void()> job)
{
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(std::move(job));
    }
    wake_.notify_one();
}

bool JobSystem::runOne()
{
    std::function<void()> job;
    {
        const std::lock_guard lock(mutex_);
        if (queue_.empty())
            return false;
        job = std::move(queue_.front());
        queue_.pop_front();
    }
    job();
    return true;
}

void JobSystem::workerLoop()
{
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        ENGINE_ZONE("Job");
        job();
    }
}

void JobSystem::parallelFor(std::size_t count, std::size_t grain, const std::function<void(std::size_t, std::size_t)>& fn)
{
    if (count == 0)
        return;
    grain = std::max<std::size_t>(1, grain);
    const std::size_t chunks = (count + grain - 1) / grain;
    if (chunks == 1 || workers_.empty()) {
        fn(0, count);
        return;
    }
    // Chunks are claimed through a shared counter by helper jobs and the caller.
    struct Shared {
        std::atomic<std::size_t> next{0};
        std::atomic<std::size_t> done{0};
    };
    auto shared = std::make_shared<Shared>();
    auto work = [shared, chunks, grain, count, &fn] {
        for (std::size_t c; (c = shared->next.fetch_add(1)) < chunks;) {
            fn(c * grain, std::min(count, (c + 1) * grain));
            shared->done.fetch_add(1);
        }
    };
    const std::size_t helpers = std::min<std::size_t>(workers_.size(), chunks - 1);
    for (std::size_t i = 0; i < helpers; ++i)
        push(work);
    work();
    // Help with other jobs while the last chunks finish (no deadlock when called from a worker).
    while (shared->done.load() < chunks)
        if (!runOne())
            std::this_thread::yield();
}

void MainThreadQueue::post(std::function<void()> fn)
{
    const std::lock_guard lock(mutex_);
    queue_.push_back(std::move(fn));
}

int MainThreadQueue::drain(double budgetMs)
{
    const auto start = std::chrono::steady_clock::now();
    int ran = 0;
    for (;;) {
        std::function<void()> fn;
        {
            const std::lock_guard lock(mutex_);
            if (queue_.empty())
                break;
            fn = std::move(queue_.front());
            queue_.pop_front();
        }
        fn();
        ++ran;
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= budgetMs)
            break;
    }
    return ran;
}

std::size_t MainThreadQueue::pending() const
{
    const std::lock_guard lock(mutex_);
    return queue_.size();
}


TaskGraph::Id TaskGraph::add(const char* name, std::function<void()> fn, std::vector<Id> after, bool mainThread)
{
    Task t;
    t.name = name;
    t.fn = std::move(fn);
    t.after = std::move(after);
    t.mainThread = mainThread;
    tasks_.push_back(std::move(t));
    return tasks_.size() - 1;
}

void TaskGraph::run(JobSystem& jobs)
{
    const std::size_t n = tasks_.size();
    if (n == 0)
        return;
    for (Task& t : tasks_)
        t.next.clear();
    for (Id i = 0; i < n; ++i)
        for (Id a : tasks_[i].after)
            tasks_[a].next.push_back(i);

    // Shared with queued jobs, which may run after run() returned (their task was
    // taken by the caller meanwhile): they only touch this state until they win a task.
    enum Phase : int { Waiting, Ready, Running, Done };
    struct State {
        explicit State(std::size_t count) : waiting(count), phase(count) {}
        std::vector<std::atomic<int>> waiting;
        std::vector<std::atomic<int>> phase;
        std::atomic<std::size_t> done{0};
        std::mutex mutex;
        std::condition_variable wake;
        std::exception_ptr error;
        std::function<void(Id)> execute;
    };
    auto state = std::make_shared<State>(n);
    State* st = state.get();
    const bool workers = jobs.threadCount() > 0;
    std::function<void(Id)> makeReady = [this, st, &jobs, workers, weak = std::weak_ptr<State>(state)](Id i) {
        st->phase[i].store(Ready);
        if (!tasks_[i].mainThread && workers)
            jobs.run([weak, i] {
                if (auto s = weak.lock()) {
                    int expected = Ready;
                    if (s->phase[i].compare_exchange_strong(expected, Running))
                        s->execute(i);
                }
            });
        std::lock_guard lock(st->mutex);
        st->wake.notify_all();
    };
    st->execute = [this, st, &makeReady](Id i) {
        const auto start = std::chrono::steady_clock::now();
        try {
            {
                ENGINE_ZONE_DYNAMIC(tasks_[i].name);
                tasks_[i].fn();
            }
        } catch (...) {
            std::lock_guard lock(st->mutex);
            if (!st->error)
                st->error = std::current_exception();
        }
        tasks_[i].ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        st->phase[i].store(Done);
        for (Id j : tasks_[i].next)
            if (st->waiting[j].fetch_sub(1) == 1)
                makeReady(j);
        st->done.fetch_add(1);
        std::lock_guard lock(st->mutex);
        st->wake.notify_all();
    };
    for (Id i = 0; i < n; ++i)
        st->waiting[i].store(static_cast<int>(tasks_[i].after.size()));
    for (Id i = 0; i < n; ++i)
        if (tasks_[i].after.empty())
            makeReady(i);

    auto take = [&](bool mainOnly) {
        for (Id i = 0; i < n; ++i) {
            if (mainOnly != tasks_[i].mainThread && (mainOnly || workers))
                continue;
            int expected = Ready;
            if (st->phase[i].load() == Ready && st->phase[i].compare_exchange_strong(expected, Running)) {
                st->execute(i);
                return true;
            }
        }
        return false;
    };
    while (st->done.load() < n) {
        // Own work first, then help with worker tasks nobody has started yet.
        if (take(true) || take(false))
            continue;
        std::unique_lock lock(st->mutex);
        st->wake.wait_for(lock, std::chrono::microseconds(500));
    }
    if (st->error)
        std::rethrow_exception(st->error);
}

} // namespace engine
