// src/core/codec/types.h
// Core types and enums for video decoder system
#pragma once

#include <cstdint>
#include <cstddef>

namespace fluxvision {

// Video codec types
enum class CodecType {
    H264,
    H265,
    UNKNOWN
};

// Stream quality levels (adaptive quality streaming)
enum class StreamQuality {
    PAUSED,      // Keyframes only, 1 FPS  -> ~10MB/camera
    THUMBNAIL,   // Sub-stream, 5 FPS      -> ~30MB/camera
    GRID_VIEW,   // Sub-stream, 10 FPS     -> ~50MB/camera
    FOCUSED,     // Main stream, 15 FPS    -> ~80MB/camera
    FULLSCREEN   // Main stream, 30 FPS    -> ~120MB/camera
};

// Pixel format for decoded frames
enum class PixelFormat {
    NV12,        // NVIDIA NVDEC output format (Y plane + interleaved UV)
    YUV420P,     // Planar YUV 4:2:0 (FFmpeg common format)
    RGBA,        // 32-bit RGBA (for rendering)
    UNKNOWN
};

// Decode result status
enum class DecodeStatus {
    SUCCESS,          // Frame decoded successfully
    NEED_MORE_DATA,   // Need more input data
    ERROR_INVALID_DATA,
    ERROR_DECODER_FAILURE,
    ERROR_OUT_OF_MEMORY
};

// Memory statistics
struct MemoryStats {
    size_t gpuMemoryUsed;      // VRAM used (bytes)
    size_t systemMemoryUsed;   // System RAM used (bytes)
    size_t surfacePoolSize;    // Number of allocated surfaces
    size_t surfacePoolCapacity; // Maximum surfaces for current quality
};

// Decoded frame metadata
struct DecodedFrame {
    uint8_t* data[3];          // Pointers to Y, U, V planes (or Y, UV for NV12)
    int pitch[3];              // Stride/pitch for each plane
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    int64_t pts;               // Presentation timestamp (microseconds)
    int64_t dts;               // Decode timestamp (microseconds)
    bool isKeyframe;

    // GPU-specific (NVDEC)
    void* cudaSurface;         // CUdeviceptr for zero-copy (nullptr for CPU)
    int cudaPitch;             // CUDA surface pitch
};

// Decode result
struct DecodeResult {
    DecodeStatus status;
    DecodedFrame* frame;       // nullptr if no frame available
    const char* errorMessage;  // nullptr if no error
};

// Decoder configuration
struct DecoderConfig {
    CodecType codec;           // H264, H265
    StreamQuality quality;     // Initial quality level
    uint32_t maxWidth;         // Max resolution (e.g., 1920 or 640)
    uint32_t maxHeight;        // Max resolution (e.g., 1080 or 360)
    bool preferHardware;       // Auto-select NVDEC if available
    bool isSubStream;          // true for grid view (640×360), false for main (1920×1080)

    // Constructor with defaults
    DecoderConfig()
        : codec(CodecType::H264)
        , quality(StreamQuality::GRID_VIEW)
        , maxWidth(1920)
        , maxHeight(1080)
        , preferHardware(true)
        , isSubStream(false)
    {}
};

// Helper functions
inline int getTargetFPS(StreamQuality quality) {
    switch (quality) {
        case StreamQuality::PAUSED:     return 1;
        case StreamQuality::THUMBNAIL:  return 5;
        case StreamQuality::GRID_VIEW:  return 10;
        case StreamQuality::FOCUSED:    return 15;
        case StreamQuality::FULLSCREEN: return 30;
        default:                        return 10;
    }
}

inline size_t getSurfacePoolSize(StreamQuality quality) {
    switch (quality) {
        case StreamQuality::PAUSED:     return 2;   // Minimal for keyframes
        case StreamQuality::THUMBNAIL:  return 4;
        case StreamQuality::GRID_VIEW:  return 4;
        case StreamQuality::FOCUSED:    return 8;
        case StreamQuality::FULLSCREEN: return 12;
        default:                        return 4;
    }
}

inline const char* codecToString(CodecType codec) {
    switch (codec) {
        case CodecType::H264:    return "H.264";
        case CodecType::H265:    return "H.265";
        case CodecType::UNKNOWN: return "Unknown";
        default:                 return "Invalid";
    }
}

inline const char* qualityToString(StreamQuality quality) {
    switch (quality) {
        case StreamQuality::PAUSED:     return "PAUSED";
        case StreamQuality::THUMBNAIL:  return "THUMBNAIL";
        case StreamQuality::GRID_VIEW:  return "GRID_VIEW";
        case StreamQuality::FOCUSED:    return "FOCUSED";
        case StreamQuality::FULLSCREEN: return "FULLSCREEN";
        default:                        return "Invalid";
    }
}

} // namespace fluxvision
