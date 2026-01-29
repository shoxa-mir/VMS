// src/core/threading/network_thread_pool.h
// Specialized thread pool for network receive operations
// Assigns cameras to threads in round-robin fashion
#pragma once

#include "thread_pool.h"
#include <unordered_map>
#include <string>
#include <atomic>

namespace fluxvision {
namespace threading {

// Specialized network thread pool with camera assignment
class NetworkThreadPool {
public:
    explicit NetworkThreadPool(size_t numThreads = 8);
    ~NetworkThreadPool();

    // Delete copy/move
    NetworkThreadPool(const NetworkThreadPool&) = delete;
    NetworkThreadPool& operator=(const NetworkThreadPool&) = delete;

    // Assign camera to a specific thread (round-robin)
    // Returns the thread ID that will handle this camera
    size_t assignCamera(const std::string& cameraId);

    // Unassign camera (for removal)
    void unassignCamera(const std::string& cameraId);

    // Get thread assignment for a camera
    size_t getCameraThread(const std::string& cameraId) const;

    // Submit task to pool (thread-safe)
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        return pool_.submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    // Shutdown pool
    void shutdown(bool waitForTasks = true);

    // Check if running
    bool isRunning() const { return pool_.isRunning(); }

    // Statistics
    ThreadPool::Stats getStats() const { return pool_.getStats(); }

    // Get number of threads
    size_t getThreadCount() const { return numThreads_; }

    // Get cameras per thread
    std::unordered_map<size_t, size_t> getCamerasPerThread() const;

private:
    ThreadPool pool_;
    size_t numThreads_;

    mutable std::mutex assignmentMutex_;
    std::unordered_map<std::string, size_t> cameraAssignments_;  // cameraId -> threadId
    std::atomic<size_t> nextThread_{0};  // Round-robin counter
};

} // namespace threading
} // namespace fluxvision
