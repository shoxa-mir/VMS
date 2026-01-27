// src/core/gpu/cuda_context.h
// CUDA context singleton - shared across all NVDEC decoders
#pragma once

#ifdef HAVE_CUDA
#include <cuda.h>
#endif

#include <mutex>

namespace fluxvision {

class CudaContext {
public:
    // Get singleton instance
    static CudaContext& getInstance();

    // Initialize CUDA context (thread-safe)
    bool initialize();

    // Check if CUDA is available and initialized
    bool isInitialized() const { return initialized_; }

#ifdef HAVE_CUDA
    // Get CUDA context (for NVDEC decoders)
    CUcontext getContext() const { return context_; }

    // Get CUDA device
    CUdevice getDevice() const { return device_; }

    // Get device properties
    int getComputeCapabilityMajor() const { return computeMajor_; }
    int getComputeCapabilityMinor() const { return computeMinor_; }
    size_t getTotalMemory() const { return totalMemory_; }
    const char* getDeviceName() const { return deviceName_; }
#endif

    // Prevent copying
    CudaContext(const CudaContext&) = delete;
    CudaContext& operator=(const CudaContext&) = delete;

private:
    CudaContext();
    ~CudaContext();

    bool initialized_;
    std::mutex initMutex_;

#ifdef HAVE_CUDA
    CUcontext context_;
    CUdevice device_;
    int computeMajor_;
    int computeMinor_;
    size_t totalMemory_;
    char deviceName_[256];
#endif
};

} // namespace fluxvision
