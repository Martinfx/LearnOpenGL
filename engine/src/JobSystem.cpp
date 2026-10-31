#include "engine/JobSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace engine {

JobSystem::JobSystem(unsigned threads)
{
    if (threads == 0) {
        const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        threads = std::max(1u, hw - 1); // the main thread works too
    }
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i)
        workers_.emplace_back([this] { workerLoop(); });
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

} // namespace engine
