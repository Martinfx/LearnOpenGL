#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace engine {

// Thread pool shared by the game and the editor: asset loading, animation,
// render preparation and other parallel work run here instead of on the
// main (UI / GL) thread. One worker per hardware thread minus the main thread.
class JobSystem {
public:
    explicit JobSystem(unsigned threads = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    static JobSystem& global();

    unsigned threadCount() const { return static_cast<unsigned>(workers_.size()); }

    // Runs `fn` on a worker; the future carries its result (or exception).
    template <class F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<std::decay_t<F>>>
    {
        using R = std::invoke_result_t<std::decay_t<F>>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        std::future<R> result = task->get_future();
        push([task] { (*task)(); });
        return result;
    }

    // Fire and forget.
    void run(std::function<void()> fn) { push(std::move(fn)); }

    // Calls fn(begin, end) over [0, count) in chunks of `grain` on all threads
    // (the calling thread helps) and returns when everything is done.
    void parallelFor(std::size_t count, std::size_t grain, const std::function<void(std::size_t, std::size_t)>& fn);

    // Runs queued jobs on the calling thread until `ready()` is true (used while waiting).
    bool runOne();

private:
    void push(std::function<void()> job);
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};

// One frame's work as tasks with dependencies ("after"). Tasks run on the job
// system as soon as what they wait for is done; main-thread tasks (GL, audio
// devices, anything not thread-safe) run on the thread that calls run(). A worker
// task that has not started when the caller is otherwise idle is run by the caller
// (no stall behind long jobs such as streaming loads). Reusable: run() again.
class TaskGraph {
public:
    using Id = std::size_t;
    Id add(const char* name, std::function<void()> fn, std::vector<Id> after = {}, bool mainThread = false);
    void run(JobSystem& jobs = JobSystem::global());
    void clear() { tasks_.clear(); }
    std::size_t size() const { return tasks_.size(); }
    // Milliseconds each task took in the last run() (profiling: FPS_ENGINE_STATS).
    double taskMs(Id id) const { return tasks_[id].ms; }
    const char* taskName(Id id) const { return tasks_[id].name; }

private:
    struct Task {
        const char* name = "";
        std::function<void()> fn;
        std::vector<Id> after, next;
        bool mainThread = false;
        double ms = 0.0;
    };
    std::vector<Task> tasks_;
};

// Results that must be applied on the main thread (GL uploads, scene changes).
// Workers post closures; the main thread drains them with a time budget.
class MainThreadQueue {
public:
    void post(std::function<void()> fn);
    // Runs posted closures for at most `budgetMs` milliseconds; returns how many ran.
    int drain(double budgetMs = 2.0);
    std::size_t pending() const;

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
};

} // namespace engine
