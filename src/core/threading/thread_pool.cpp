// src/core/threading/thread_pool.cpp
#include "thread_pool.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace fluxvision {
namespace threading {

ThreadPool::ThreadPool(const Config& config)
    : config_(config)
{
    workers_.reserve(config_.numThreads);

    for (size_t i = 0; i < config_.numThreads; ++i) {
        auto worker = std::make_unique<Worker>();
        worker->workerId = i;
        worker->thread = std::thread([this, i]() { workerLoop(i); });

        if (config_.enableAffinity) {
            setCpuAffinity(i);
        }

        workers_.push_back(std::move(worker));
    }
}

ThreadPool::~ThreadPool() {
    shutdown(true);
}

void ThreadPool::shutdown(bool waitForTasks) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;

        if (!waitForTasks) {
            // Clear pending tasks
            std::queue<std::function<void()>> empty;
            std::swap(tasks_, empty);
        }
    }

    condition_.notify_all();

    // Join all threads
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

void ThreadPool::workerLoop(size_t workerId) {
    Worker& worker = *workers_[workerId];

    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            condition_.wait(lock, [this] {
                return !running_ || !tasks_.empty();
            });

            if (!running_ && tasks_.empty()) {
                break;
            }

            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }

        if (task) {
            task();
            worker.tasksProcessed++;
            tasksCompleted_++;
        }
    }
}

void ThreadPool::setCpuAffinity(size_t threadId) {
#ifdef _WIN32
    // Windows: Set thread affinity
    DWORD_PTR mask = static_cast<DWORD_PTR>(1) << (threadId % std::thread::hardware_concurrency());
    SetThreadAffinityMask(workers_[threadId]->thread.native_handle(), mask);
#else
    // Linux: Set thread affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(threadId % std::thread::hardware_concurrency(), &cpuset);
    pthread_setaffinity_np(workers_[threadId]->thread.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
}

ThreadPool::Stats ThreadPool::getStats() const {
    Stats stats;

    // Atomic snapshot
    stats.tasksSubmitted = tasksSubmitted_.load();
    stats.tasksCompleted = tasksCompleted_.load();

    {
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        stats.tasksInQueue = tasks_.size();
    }

    // Per-thread stats
    stats.perThreadTaskCount.reserve(workers_.size());
    for (const auto& worker : workers_) {
        stats.perThreadTaskCount.push_back(worker->tasksProcessed.load());
    }

    return stats;
}

} // namespace threading
} // namespace fluxvision
