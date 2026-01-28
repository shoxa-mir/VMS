// src/core/codec/decoder_factory.cpp
// Factory for creating appropriate decoder
#include "decoder_factory.h"
#include "nvdec_decoder.h"
#include "cpu_decoder.h"
#include "../gpu/cuda_context.h"
#include <iostream>

namespace fluxvision {

std::unique_ptr<IDecoder> DecoderFactory::create(const DecoderConfig& config) {
    // Auto-select based on preference and availability
    if (config.preferHardware && isNvdecAvailable()) {
        return create(DecoderType::NVDEC, config);
    } else {
        return create(DecoderType::CPU, config);
    }
}

std::unique_ptr<IDecoder> DecoderFactory::create(DecoderType type, const DecoderConfig& config) {
    std::unique_ptr<IDecoder> decoder;

    switch (type) {
        case DecoderType::AUTO:
            return create(config);  // Recursive call to auto-select

        case DecoderType::NVDEC:
            if (!isNvdecAvailable()) {
                std::cerr << "DecoderFactory: NVDEC requested but not available, falling back to CPU" << std::endl;
                decoder = std::make_unique<CpuDecoder>();
            } else {
                decoder = std::make_unique<NvdecDecoder>();
            }
            break;

        case DecoderType::CPU:
            decoder = std::make_unique<CpuDecoder>();
            break;

        default:
            std::cerr << "DecoderFactory: Unknown decoder type, using CPU" << std::endl;
            decoder = std::make_unique<CpuDecoder>();
            break;
    }

    // Initialize the decoder
    if (decoder && !decoder->initialize(config)) {
        std::cerr << "DecoderFactory: Failed to initialize decoder" << std::endl;
        return nullptr;
    }

    return decoder;
}

bool DecoderFactory::isNvdecAvailable() {
#ifdef HAVE_CUDA
    // Check if CUDA context can be initialized
    auto& cudaCtx = CudaContext::getInstance();
    if (!cudaCtx.isInitialized()) {
        // Try to initialize
        if (!cudaCtx.initialize()) {
            return false;
        }
    }

    // If CUDA is available, NVDEC should be available
    // (Assuming NVDEC SDK headers are available at compile time)
    return cudaCtx.isInitialized();
#else
    return false;
#endif
}

DecoderType DecoderFactory::getRecommendedType() {
    if (isNvdecAvailable()) {
        return DecoderType::NVDEC;
    }
    return DecoderType::CPU;
}

DecoderFactory::DecoderCapabilities DecoderFactory::getCapabilities() {
    DecoderCapabilities caps = {};

#ifdef HAVE_CUDA
    caps.nvdecAvailable = isNvdecAvailable();

    auto& cudaCtx = CudaContext::getInstance();
    if (cudaCtx.isInitialized()) {
        caps.cudaDeviceCount = 1;  // Simplified: we use device 0
    } else {
        caps.cudaDeviceCount = 0;
    }
#else
    caps.nvdecAvailable = false;
    caps.cudaDeviceCount = 0;
#endif

    // CPU decoder is always available (FFmpeg is a required dependency)
    caps.cpuDecoderAvailable = true;

    if (caps.nvdecAvailable) {
        caps.recommendedDecoder = "NVDEC (Hardware)";
    } else {
        caps.recommendedDecoder = "CPU (Software)";
    }

    return caps;
}

} // namespace fluxvision
