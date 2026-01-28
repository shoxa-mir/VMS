// src/core/codec/nvdec_decoder.h
// NVIDIA NVDEC hardware decoder implementation
#pragma once

#include "decoder_interface.h"
#include <cuda.h>
#include <nvcuvid.h>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>

namespace fluxvision {

class NvdecDecoder : public IDecoder {
public:
    NvdecDecoder();
    ~NvdecDecoder() override;

    // IDecoder interface
    bool initialize(const DecoderConfig& config) override;
    DecodeResult decode(const uint8_t* data, size_t size) override;
    DecodedFrame* getFrame() override;
    void setQuality(StreamQuality quality) override;
    MemoryStats getMemoryUsage() const override;
    void flush() override;
    void reset() override;
    const DecoderConfig& getConfig() const override { return config_; }
    bool isHardwareAccelerated() const override { return true; }

private:
    // NVDEC callbacks
    static int CUDAAPI handleVideoSequence(void* userData, CUVIDEOFORMAT* format);
    static int CUDAAPI handlePictureDecode(void* userData, CUVIDPICPARAMS* picParams);
    static int CUDAAPI handlePictureDisplay(void* userData, CUVIDPARSERDISPINFO* dispInfo);

    // Internal helpers
    bool createParser();
    bool createDecoder(CUVIDEOFORMAT* format);
    void destroyParser();
    void destroyDecoder();
    bool allocateSurfaces();
    void freeSurfaces();

    DecodedFrame* mapSurfaceToFrame(CUdeviceptr surface, CUVIDPARSERDISPINFO* dispInfo);

    // Configuration
    DecoderConfig config_;

    // CUDA context (shared singleton)
    CUcontext cudaContext_;

    // NVDEC parser and decoder
    CUvideoparser parser_;
    CUvideodecoder decoder_;

    // Decoder creation parameters
    CUVIDDECODECREATEINFO decoderInfo_;

    // Surface pool for decoded frames
    struct Surface {
        CUdeviceptr devicePtr;
        size_t pitch;
        bool inUse;
    };
    std::vector<Surface> surfaces_;

    // Frame queue
    struct FrameInfo {
        int surfaceIndex;
        int64_t pts;
        bool isKeyframe;
    };
    std::queue<FrameInfo> frameQueue_;
    std::mutex frameMutex_;

    // Current frame being accessed
    DecodedFrame currentFrame_;

    // Statistics
    mutable std::mutex statsMutex_;
    size_t totalMemoryAllocated_;
    size_t framesDecoded_;

    // Initialization state
    bool initialized_;
};

} // namespace fluxvision
