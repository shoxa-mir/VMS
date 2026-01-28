// src/core/codec/cpu_decoder.cpp
// CPU-based software decoder using FFmpeg libavcodec
#include "cpu_decoder.h"
#include <cstring>
#include <iostream>

namespace fluxvision {

CpuDecoder::CpuDecoder()
    : codec_(nullptr)
    , codecCtx_(nullptr)
    , packet_(nullptr)
    , avFrame_(nullptr)
    , frameAvailable_(false)
    , systemMemoryUsed_(0)
    , framesDecoded_(0)
    , initialized_(false)
{
    std::memset(&currentFrame_, 0, sizeof(currentFrame_));
}

CpuDecoder::~CpuDecoder() {
    freeFrame();

    if (packet_) {
        av_packet_free(&packet_);
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool CpuDecoder::initialize(const DecoderConfig& config) {
    if (initialized_) {
        reset();
    }

    config_ = config;

    // Find decoder
    AVCodecID codecId = (config_.codec == CodecType::H265) ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    codec_ = avcodec_find_decoder(codecId);
    if (!codec_) {
        std::cerr << "CpuDecoder: Failed to find " << codecToString(config_.codec) << " decoder" << std::endl;
        return false;
    }

    // Allocate codec context
    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) {
        std::cerr << "CpuDecoder: Failed to allocate codec context" << std::endl;
        return false;
    }

    // Configure codec context for low latency
    codecCtx_->thread_count = 2;  // Limited threading for CPU decoder
    codecCtx_->thread_type = FF_THREAD_SLICE;
    codecCtx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecCtx_->flags2 |= AV_CODEC_FLAG2_FAST;

    // Set maximum resolution
    codecCtx_->coded_width = config_.maxWidth;
    codecCtx_->coded_height = config_.maxHeight;

    // Open codec
    int ret = avcodec_open2(codecCtx_, codec_, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        std::cerr << "CpuDecoder: Failed to open codec: " << errBuf << std::endl;
        avcodec_free_context(&codecCtx_);
        return false;
    }

    // Allocate packet
    packet_ = av_packet_alloc();
    if (!packet_) {
        std::cerr << "CpuDecoder: Failed to allocate packet" << std::endl;
        avcodec_free_context(&codecCtx_);
        return false;
    }

    // Allocate frame
    if (!allocateFrame()) {
        av_packet_free(&packet_);
        avcodec_free_context(&codecCtx_);
        return false;
    }

    initialized_ = true;
    std::cout << "CpuDecoder: Initialized " << codecToString(config_.codec)
              << " decoder (software fallback)" << std::endl;

    return true;
}

bool CpuDecoder::allocateFrame() {
    avFrame_ = av_frame_alloc();
    if (!avFrame_) {
        std::cerr << "CpuDecoder: Failed to allocate frame" << std::endl;
        return false;
    }

    return true;
}

void CpuDecoder::freeFrame() {
    if (avFrame_) {
        av_frame_free(&avFrame_);
    }
}

DecodeResult CpuDecoder::decode(const uint8_t* data, size_t size) {
    DecodeResult result = {};
    result.status = DecodeStatus::NEED_MORE_DATA;
    result.frame = nullptr;
    result.errorMessage = nullptr;

    if (!initialized_) {
        result.status = DecodeStatus::ERROR_DECODER_FAILURE;
        result.errorMessage = "Decoder not initialized";
        return result;
    }

    // Prepare packet
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = size;

    // Send packet to decoder
    int ret = avcodec_send_packet(codecCtx_, packet_);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            result.status = DecodeStatus::NEED_MORE_DATA;
        } else if (ret == AVERROR_EOF) {
            result.status = DecodeStatus::SUCCESS;
        } else {
            result.status = DecodeStatus::ERROR_INVALID_DATA;
            result.errorMessage = "avcodec_send_packet failed";
        }
        return result;
    }

    // Receive decoded frame
    ret = avcodec_receive_frame(codecCtx_, avFrame_);
    if (ret == 0) {
        // Frame decoded successfully
        frameAvailable_ = true;
        framesDecoded_++;
        result.status = DecodeStatus::SUCCESS;
    } else if (ret == AVERROR(EAGAIN)) {
        // Need more data
        result.status = DecodeStatus::NEED_MORE_DATA;
    } else if (ret == AVERROR_EOF) {
        // End of stream
        result.status = DecodeStatus::SUCCESS;
    } else {
        result.status = DecodeStatus::ERROR_DECODER_FAILURE;
        result.errorMessage = "avcodec_receive_frame failed";
    }

    return result;
}

DecodedFrame* CpuDecoder::getFrame() {
    if (!frameAvailable_ || !avFrame_) {
        return nullptr;
    }

    // Map AVFrame to DecodedFrame
    currentFrame_.width = avFrame_->width;
    currentFrame_.height = avFrame_->height;
    currentFrame_.pts = avFrame_->pts;
    currentFrame_.dts = avFrame_->pkt_dts;
    // FFmpeg 6.0+ API: use flags instead of deprecated key_frame field
    currentFrame_.isKeyframe = (avFrame_->flags & AV_FRAME_FLAG_KEY);

    // Determine pixel format
    switch (avFrame_->format) {
        case AV_PIX_FMT_YUV420P:
            currentFrame_.format = PixelFormat::YUV420P;
            currentFrame_.data[0] = avFrame_->data[0];  // Y plane
            currentFrame_.data[1] = avFrame_->data[1];  // U plane
            currentFrame_.data[2] = avFrame_->data[2];  // V plane
            currentFrame_.pitch[0] = avFrame_->linesize[0];
            currentFrame_.pitch[1] = avFrame_->linesize[1];
            currentFrame_.pitch[2] = avFrame_->linesize[2];
            break;

        case AV_PIX_FMT_NV12:
            currentFrame_.format = PixelFormat::NV12;
            currentFrame_.data[0] = avFrame_->data[0];  // Y plane
            currentFrame_.data[1] = avFrame_->data[1];  // UV plane (interleaved)
            currentFrame_.data[2] = nullptr;
            currentFrame_.pitch[0] = avFrame_->linesize[0];
            currentFrame_.pitch[1] = avFrame_->linesize[1];
            currentFrame_.pitch[2] = 0;
            break;

        default:
            std::cerr << "CpuDecoder: Unsupported pixel format: " << avFrame_->format << std::endl;
            return nullptr;
    }

    // CPU decoder doesn't use GPU memory
    currentFrame_.cudaSurface = nullptr;
    currentFrame_.cudaPitch = 0;

    frameAvailable_ = false;  // Mark frame as consumed
    return &currentFrame_;
}

void CpuDecoder::setQuality(StreamQuality quality) {
    // Quality changes don't affect CPU decoder significantly
    // We just update the config
    config_.quality = quality;
}

MemoryStats CpuDecoder::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    MemoryStats stats = {};
    stats.gpuMemoryUsed = 0;  // CPU decoder doesn't use GPU
    stats.systemMemoryUsed = systemMemoryUsed_;

    if (avFrame_) {
        // Estimate frame buffer size (YUV420P format)
        stats.systemMemoryUsed += (config_.maxWidth * config_.maxHeight * 3 / 2);
    }

    stats.surfacePoolSize = 1;  // CPU decoder uses single frame
    stats.surfacePoolCapacity = 1;

    return stats;
}

void CpuDecoder::flush() {
    if (!codecCtx_) {
        return;
    }

    // Flush decoder
    avcodec_flush_buffers(codecCtx_);
    frameAvailable_ = false;
}

void CpuDecoder::reset() {
    flush();
    framesDecoded_ = 0;
}

} // namespace fluxvision
