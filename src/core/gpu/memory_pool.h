// src/core/gpu/memory_pool.h
// GPU memory pool manager for centralized VRAM tracking
// Note: NVDEC decoders maintain their own surface pools (API requirement)
// This pool provides centralized statistics and monitoring
#pragma once

#include "../codec/types.h"
#include <string>
#include <map>
#include <mutex>
#include <atomic>

#ifdef HAVE_CUDA
#include <cuda.h>
#endif

namespace fluxvision {
namespace gpu {

// GPU Memory Pool - Centralized VRAM tracking and statistics
class GPUMemoryPool {
public:
    struct Config {
        size_t maxGpuMemoryBytes = 3ULL * 1024 * 1024 * 1024;  // 3GB limit
        bool enableWarnings = true;
    };

    struct Stats {
        size_t totalAllocatedBytes = 0;
        size_t peakAllocatedBytes = 0;
        size_t totalSurfaceCount = 0;
        std::map<std::string, size_t> perCameraMemoryBytes;  // cameraId -> bytes
        std::map<std::string, size_t> perCameraSurfaceCount; // cameraId -> count
        double utilizationPercent = 0.0;
    };

    explicit GPUMemoryPool(const Config& config);
    ~GPUMemoryPool();

    // Delete copy/move
    GPUMemoryPool(const GPUMemoryPool&) = delete;
    GPUMemoryPool& operator=(const GPUMemoryPool&) = delete;

    // Initialize pool
    bool initialize();

    // Register camera allocation (called by decoders)
    void registerAllocation(const std::string& cameraId, size_t bytes, size_t surfaceCount);

    // Unregister camera allocation (called on decoder cleanup)
    void unregisterAllocation(const std::string& cameraId);

    // Update camera allocation (called on quality change)
    void updateAllocation(const std::string& cameraId, size_t newBytes, size_t newSurfaceCount);

    // Get statistics
    Stats getStats() const;

    // Check if allocation would exceed limit
    bool wouldExceedLimit(size_t additionalBytes) const;

    // Get available memory
    size_t getAvailableMemory() const;

private:
    Config config_;
    bool initialized_;

    mutable std::mutex statsMutex_;
    std::atomic<size_t> totalAllocatedBytes_{0};
    std::atomic<size_t> peakAllocatedBytes_{0};
    std::map<std::string, size_t> perCameraMemory_;
    std::map<std::string, size_t> perCameraSurfaces_;

    void checkMemoryLimits();
};

} // namespace gpu
} // namespace fluxvision
