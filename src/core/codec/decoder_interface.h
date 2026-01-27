// src/core/codec/decoder_interface.h
// Abstract interface for video decoders (NVDEC and FFmpeg)
#pragma once

#include "types.h"
#include <memory>

namespace fluxvision {

class IDecoder {
public:
    virtual ~IDecoder() = default;

    // Initialize decoder with configuration
    // Returns true on success, false on failure
    virtual bool initialize(const DecoderConfig& config) = 0;

    // Decode a chunk of encoded data
    // data: pointer to encoded bitstream (H.264/H.265)
    // size: size of data in bytes
    // Returns DecodeResult with status and frame (if available)
    virtual DecodeResult decode(const uint8_t* data, size_t size) = 0;

    // Get the most recent decoded frame
    // Returns nullptr if no frame is available
    // Note: Frame ownership remains with decoder (zero-copy)
    virtual DecodedFrame* getFrame() = 0;

    // Dynamically change quality level
    // This will resize surface pools and adjust FPS limiting
    // quality: new quality level
    virtual void setQuality(StreamQuality quality) = 0;

    // Get current memory usage statistics
    virtual MemoryStats getMemoryUsage() const = 0;

    // Flush decoder (process any remaining frames)
    virtual void flush() = 0;

    // Reset decoder state
    virtual void reset() = 0;

    // Get current configuration
    virtual const DecoderConfig& getConfig() const = 0;

    // Check if decoder is hardware-accelerated
    virtual bool isHardwareAccelerated() const = 0;
};

} // namespace fluxvision
