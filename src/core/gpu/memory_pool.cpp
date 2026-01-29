// src/core/gpu/memory_pool.cpp
#include "memory_pool.h"
#include <iostream>

namespace fluxvision {
namespace gpu {

GPUMemoryPool::GPUMemoryPool(const Config& config)
    : config_(config)
    , initialized_(false)
{
}

GPUMemoryPool::~GPUMemoryPool() {
    // Cleanup handled by individual decoders
}

bool GPUMemoryPool::initialize() {
    if (initialized_) {
        return true;
    }

    initialized_ = true;
    return true;
}

void GPUMemoryPool::registerAllocation(const std::string& cameraId, size_t bytes, size_t surfaceCount) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    // Update per-camera tracking
    perCameraMemory_[cameraId] = bytes;
    perCameraSurfaces_[cameraId] = surfaceCount;

    // Update totals
    size_t oldTotal = totalAllocatedBytes_.load();
    size_t newTotal = oldTotal + bytes;
    totalAllocatedBytes_.store(newTotal);

    // Update peak
    size_t peak = peakAllocatedBytes_.load();
    if (newTotal > peak) {
        peakAllocatedBytes_.store(newTotal);
    }

    checkMemoryLimits();
}

void GPUMemoryPool::unregisterAllocation(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = perCameraMemory_.find(cameraId);
    if (it != perCameraMemory_.end()) {
        size_t bytes = it->second;

        // Update total
        size_t oldTotal = totalAllocatedBytes_.load();
        if (oldTotal >= bytes) {
            totalAllocatedBytes_.store(oldTotal - bytes);
        }

        perCameraMemory_.erase(it);
        perCameraSurfaces_.erase(cameraId);
    }
}

void GPUMemoryPool::updateAllocation(const std::string& cameraId, size_t newBytes, size_t newSurfaceCount) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = perCameraMemory_.find(cameraId);
    if (it != perCameraMemory_.end()) {
        size_t oldBytes = it->second;

        // Update total
        size_t oldTotal = totalAllocatedBytes_.load();
        size_t newTotal = oldTotal - oldBytes + newBytes;
        totalAllocatedBytes_.store(newTotal);

        // Update per-camera
        perCameraMemory_[cameraId] = newBytes;
        perCameraSurfaces_[cameraId] = newSurfaceCount;

        // Update peak
        size_t peak = peakAllocatedBytes_.load();
        if (newTotal > peak) {
            peakAllocatedBytes_.store(newTotal);
        }

        checkMemoryLimits();
    } else {
        // Not found, treat as new allocation
        registerAllocation(cameraId, newBytes, newSurfaceCount);
    }
}

GPUMemoryPool::Stats GPUMemoryPool::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    Stats stats;
    stats.totalAllocatedBytes = totalAllocatedBytes_.load();
    stats.peakAllocatedBytes = peakAllocatedBytes_.load();

    stats.totalSurfaceCount = 0;
    for (const auto& entry : perCameraSurfaces_) {
        stats.totalSurfaceCount += entry.second;
    }

    stats.perCameraMemoryBytes = perCameraMemory_;
    stats.perCameraSurfaceCount = perCameraSurfaces_;

    if (config_.maxGpuMemoryBytes > 0) {
        stats.utilizationPercent = (static_cast<double>(stats.totalAllocatedBytes) /
                                   config_.maxGpuMemoryBytes) * 100.0;
    }

    return stats;
}

bool GPUMemoryPool::wouldExceedLimit(size_t additionalBytes) const {
    size_t current = totalAllocatedBytes_.load();
    return (current + additionalBytes) > config_.maxGpuMemoryBytes;
}

size_t GPUMemoryPool::getAvailableMemory() const {
    size_t current = totalAllocatedBytes_.load();
    if (current >= config_.maxGpuMemoryBytes) {
        return 0;
    }
    return config_.maxGpuMemoryBytes - current;
}

void GPUMemoryPool::checkMemoryLimits() {
    if (!config_.enableWarnings) {
        return;
    }

    size_t current = totalAllocatedBytes_.load();
    double utilizationPercent = (static_cast<double>(current) / config_.maxGpuMemoryBytes) * 100.0;

    if (utilizationPercent > 90.0) {
        std::cerr << "WARNING: GPU memory usage at " << utilizationPercent << "% ("
                  << (current / (1024 * 1024)) << " MB / "
                  << (config_.maxGpuMemoryBytes / (1024 * 1024)) << " MB)" << std::endl;
    }
}

} // namespace gpu
} // namespace fluxvision
