// src/core/threading/network_thread_pool.cpp
#include "network_thread_pool.h"
#include <iostream>

namespace fluxvision {
namespace threading {

NetworkThreadPool::NetworkThreadPool(size_t numThreads)
    : pool_(ThreadPool::Config{numThreads, "NetworkPool", false})
    , numThreads_(numThreads)
{
}

NetworkThreadPool::~NetworkThreadPool() {
    shutdown(true);
}

size_t NetworkThreadPool::assignCamera(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(assignmentMutex_);

    // Check if already assigned
    auto it = cameraAssignments_.find(cameraId);
    if (it != cameraAssignments_.end()) {
        return it->second;  // Return existing assignment
    }

    // Round-robin assignment
    size_t threadId = nextThread_.fetch_add(1) % numThreads_;
    cameraAssignments_[cameraId] = threadId;

    return threadId;
}

void NetworkThreadPool::unassignCamera(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(assignmentMutex_);
    cameraAssignments_.erase(cameraId);
}

size_t NetworkThreadPool::getCameraThread(const std::string& cameraId) const {
    std::lock_guard<std::mutex> lock(assignmentMutex_);

    auto it = cameraAssignments_.find(cameraId);
    if (it != cameraAssignments_.end()) {
        return it->second;
    }

    return 0;  // Default to thread 0 if not found
}

void NetworkThreadPool::shutdown(bool waitForTasks) {
    pool_.shutdown(waitForTasks);
}

std::unordered_map<size_t, size_t> NetworkThreadPool::getCamerasPerThread() const {
    std::lock_guard<std::mutex> lock(assignmentMutex_);

    std::unordered_map<size_t, size_t> counts;
    for (const auto& assignment : cameraAssignments_) {
        counts[assignment.second]++;
    }

    return counts;
}

} // namespace threading
} // namespace fluxvision
