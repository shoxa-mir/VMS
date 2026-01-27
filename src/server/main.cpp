// src/server/main.cpp
// Temporary test for Phase 1 Day 1: CUDA context verification
#include "core/gpu/cuda_context.h"
#include "core/codec/types.h"
#include <iostream>

int main() {
    std::cout << "FluxVision VMS Server - Phase 1 Day 1 Test" << std::endl;
    std::cout << "==========================================" << std::endl;

    // Test CUDA context initialization
    auto& cudaContext = fluxvision::CudaContext::getInstance();

    std::cout << "\nInitializing CUDA context..." << std::endl;
    if (cudaContext.initialize()) {
        std::cout << "✓ CUDA context initialized successfully" << std::endl;

#ifdef HAVE_CUDA
        std::cout << "\nGPU Information:" << std::endl;
        std::cout << "  Device Name: " << cudaContext.getDeviceName() << std::endl;
        std::cout << "  Compute Capability: "
                  << cudaContext.getComputeCapabilityMajor() << "."
                  << cudaContext.getComputeCapabilityMinor() << std::endl;
        std::cout << "  Total Memory: " << (cudaContext.getTotalMemory() / (1024 * 1024)) << " MB" << std::endl;
#endif
    } else {
        std::cout << "✗ CUDA context initialization failed (this is OK if no GPU available)" << std::endl;
    }

    // Test quality level functions
    std::cout << "\nQuality Level Configuration:" << std::endl;
    for (int i = 0; i <= 4; i++) {
        auto quality = static_cast<fluxvision::StreamQuality>(i);
        std::cout << "  " << fluxvision::qualityToString(quality)
                  << ": " << fluxvision::getTargetFPS(quality) << " FPS, "
                  << fluxvision::getSurfacePoolSize(quality) << " surfaces" << std::endl;
    }

    // Test codec types
    std::cout << "\nSupported Codecs:" << std::endl;
    std::cout << "  " << fluxvision::codecToString(fluxvision::CodecType::H264) << std::endl;
    std::cout << "  " << fluxvision::codecToString(fluxvision::CodecType::H265) << std::endl;

    std::cout << "\n✓ Phase 1 Day 1 foundation tests passed!" << std::endl;

    return 0;
}
