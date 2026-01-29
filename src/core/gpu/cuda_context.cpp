// src/core/gpu/cuda_context.cpp
#include "cuda_context.h"
#include <cstring>
#include <iostream>

namespace fluxvision {

CudaContext::CudaContext()
    : initialized_(false)
#ifdef HAVE_CUDA
    , context_(nullptr)
    , device_(0)
    , computeMajor_(0)
    , computeMinor_(0)
    , totalMemory_(0)
#endif
{
#ifdef HAVE_CUDA
    std::memset(deviceName_, 0, sizeof(deviceName_));
#endif
}

CudaContext::~CudaContext() {
#ifdef HAVE_CUDA
    if (initialized_ && context_) {
        cuCtxDestroy(context_);
        context_ = nullptr;
    }
#endif
    initialized_ = false;
}

CudaContext& CudaContext::getInstance() {
    static CudaContext instance;
    return instance;
}

bool CudaContext::initialize() {
    std::lock_guard<std::mutex> lock(initMutex_);

    if (initialized_) {
        return true;  // Already initialized
    }

#ifdef HAVE_CUDA
    // Initialize CUDA driver API
    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "CUDA initialization failed: " << (errorStr ? errorStr : "Unknown error") << std::endl;
        return false;
    }

    // Get device count
    int deviceCount = 0;
    result = cuDeviceGetCount(&deviceCount);
    if (result != CUDA_SUCCESS || deviceCount == 0) {
        std::cerr << "No CUDA devices found" << std::endl;
        return false;
    }

    // Use first device (device 0)
    result = cuDeviceGet(&device_, 0);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "Failed to get CUDA device: " << (errorStr ? errorStr : "Unknown error") << std::endl;
        return false;
    }

    // Get device properties
    cuDeviceGetName(deviceName_, sizeof(deviceName_), device_);
    cuDeviceGetAttribute(&computeMajor_, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device_);
    cuDeviceGetAttribute(&computeMinor_, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device_);
    cuDeviceTotalMem(&totalMemory_, device_);

    // Create CUDA context
    result = cuCtxCreate(&context_, 0, device_);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "Failed to create CUDA context: " << (errorStr ? errorStr : "Unknown error") << std::endl;
        return false;
    }

    initialized_ = true;

    std::cout << "CUDA initialized successfully:" << std::endl;
    std::cout << "  Device: " << deviceName_ << std::endl;
    std::cout << "  Compute Capability: " << computeMajor_ << "." << computeMinor_ << std::endl;
    std::cout << "  Total Memory: " << (totalMemory_ / (1024 * 1024)) << " MB" << std::endl;

    return true;
#else
    std::cerr << "CUDA support not compiled in (HAVE_CUDA not defined)" << std::endl;
    return false;
#endif
}

#ifdef HAVE_CUDA
// Phase 3: Multi-context support for decode thread pool
CUcontext CudaContext::createContext(int deviceId) {
    // Ensure CUDA is initialized
    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "CudaContext::createContext - CUDA init failed: "
                  << (errorStr ? errorStr : "Unknown error") << std::endl;
        return nullptr;
    }

    // Get device
    CUdevice device;
    result = cuDeviceGet(&device, deviceId);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "CudaContext::createContext - Failed to get device " << deviceId << ": "
                  << (errorStr ? errorStr : "Unknown error") << std::endl;
        return nullptr;
    }

    // Create context
    CUcontext context;
    result = cuCtxCreate(&context, 0, device);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "CudaContext::createContext - Failed to create context: "
                  << (errorStr ? errorStr : "Unknown error") << std::endl;
        return nullptr;
    }

    return context;
}

void CudaContext::destroyContext(CUcontext context) {
    if (context) {
        CUresult result = cuCtxDestroy(context);
        if (result != CUDA_SUCCESS) {
            const char* errorStr = nullptr;
            cuGetErrorString(result, &errorStr);
            std::cerr << "CudaContext::destroyContext - Failed to destroy context: "
                      << (errorStr ? errorStr : "Unknown error") << std::endl;
        }
    }
}
#endif

} // namespace fluxvision
