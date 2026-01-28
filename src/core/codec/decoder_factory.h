// src/core/codec/decoder_factory.h
// Factory for creating appropriate decoder (NVDEC or CPU fallback)
#pragma once

#include "decoder_interface.h"
#include <memory>

namespace fluxvision {

// Decoder type enum
enum class DecoderType {
    AUTO,        // Auto-select (prefer NVDEC, fallback to CPU)
    NVDEC,       // Force NVIDIA hardware decoder
    CPU          // Force CPU software decoder
};

class DecoderFactory {
public:
    // Create decoder with automatic hardware detection
    // Returns NVDEC if available and config.preferHardware is true, otherwise CPU decoder
    static std::unique_ptr<IDecoder> create(const DecoderConfig& config);

    // Create specific decoder type
    static std::unique_ptr<IDecoder> create(DecoderType type, const DecoderConfig& config);

    // Check if NVDEC is available on this system
    static bool isNvdecAvailable();

    // Get recommended decoder type for current system
    static DecoderType getRecommendedType();

    // Get decoder capabilities info
    struct DecoderCapabilities {
        bool nvdecAvailable;
        bool cpuDecoderAvailable;
        int cudaDeviceCount;
        const char* recommendedDecoder;
    };

    static DecoderCapabilities getCapabilities();

private:
    // Prevent instantiation
    DecoderFactory() = default;
};

} // namespace fluxvision
