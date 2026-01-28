// src/core/codec/cpu_decoder.h
// CPU-based software decoder using FFmpeg libavcodec (fallback when no GPU)
#pragma once

#include "decoder_interface.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <memory>
#include <mutex>

namespace fluxvision {

class CpuDecoder : public IDecoder {
public:
    CpuDecoder();
    ~CpuDecoder() override;

    // IDecoder interface
    bool initialize(const DecoderConfig& config) override;
    DecodeResult decode(const uint8_t* data, size_t size) override;
    DecodedFrame* getFrame() override;
    void setQuality(StreamQuality quality) override;
    MemoryStats getMemoryUsage() const override;
    void flush() override;
    void reset() override;
    const DecoderConfig& getConfig() const override { return config_; }
    bool isHardwareAccelerated() const override { return false; }

private:
    bool allocateFrame();
    void freeFrame();

    // Configuration
    DecoderConfig config_;

    // FFmpeg codec context
    const AVCodec* codec_;
    AVCodecContext* codecCtx_;
    AVPacket* packet_;
    AVFrame* avFrame_;

    // Current decoded frame (for getFrame())
    DecodedFrame currentFrame_;
    bool frameAvailable_;

    // Statistics
    mutable std::mutex statsMutex_;
    size_t systemMemoryUsed_;
    size_t framesDecoded_;

    // Initialization state
    bool initialized_;
};

} // namespace fluxvision
