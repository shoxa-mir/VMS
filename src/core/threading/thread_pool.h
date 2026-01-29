// src/core/threading/thread_pool.h
// Generic thread pool with work queue for task-based parallelism
#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <string>

namespace fluxvision {
namespace threading {

// Generic thread pool with task queue
class ThreadPool {
public:
    struct Config {
        size_t numThreads = 4;
        std::string name = "ThreadPool";
        bool enableAffinity = false;  // CPU affinity (optional optimization)
    };

    struct Stats {
        size_t tasksSubmitted = 0;
        size_t tasksCompleted = 0;
        size_t tasksInQueue = 0;
        std::vector<size_t> perThreadTaskCount;
    };

    explicit ThreadPool(const Config& config);
    ~ThreadPool();

    // Delete copy/move
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit task to pool (thread-safe)
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Shutdown pool
    void shutdown(bool waitForTasks = true);

    // Check if running
    bool isRunning() const { return running_.load(); }

    // Statistics
    Stats getStats() const;

private:
    struct Worker {
        std::thread thread;
        std::atomic<size_t> tasksProcessed{0};
        size_t workerId = 0;
    };

    Config config_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> running_{true};

    // Statistics (atomic for thread-safe updates)
    std::atomic<size_t> tasksSubmitted_{0};
    std::atomic<size_t> tasksCompleted_{0};

    void workerLoop(size_t workerId);
    void setCpuAffinity(size_t threadId);
};

// Template implementation
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (!running_) {
            throw std::runtime_error("ThreadPool: Cannot submit task to stopped pool");
        }

        tasks_.emplace([task]() { (*task)(); });
        tasksSubmitted_++;
    }

    condition_.notify_one();
    return result;
}

} // namespace threading
} // namespace fluxvision
