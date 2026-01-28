// src/core/codec/nvdec_decoder.cpp
// NVIDIA NVDEC hardware decoder implementation
#include "nvdec_decoder.h"
#include "../gpu/cuda_context.h"
#include <cstring>
#include <iostream>

namespace fluxvision {

NvdecDecoder::NvdecDecoder()
    : cudaContext_(nullptr)
    , parser_(nullptr)
    , decoder_(nullptr)
    , totalMemoryAllocated_(0)
    , framesDecoded_(0)
    , initialized_(false)
{
    std::memset(&decoderInfo_, 0, sizeof(decoderInfo_));
    std::memset(&currentFrame_, 0, sizeof(currentFrame_));
}

NvdecDecoder::~NvdecDecoder() {
    destroyDecoder();
    destroyParser();
    freeSurfaces();
}

bool NvdecDecoder::initialize(const DecoderConfig& config) {
    if (initialized_) {
        reset();
    }

    config_ = config;

    // Get CUDA context from singleton
    auto& cudaCtx = CudaContext::getInstance();
    if (!cudaCtx.isInitialized() && !cudaCtx.initialize()) {
        std::cerr << "NvdecDecoder: Failed to initialize CUDA context" << std::endl;
        return false;
    }

#ifdef HAVE_CUDA
    cudaContext_ = cudaCtx.getContext();

    // Set CUDA context as current
    CUresult cuResult = cuCtxPushCurrent(cudaContext_);
    if (cuResult != CUDA_SUCCESS) {
        std::cerr << "NvdecDecoder: Failed to set CUDA context" << std::endl;
        return false;
    }
#else
    std::cerr << "NvdecDecoder: CUDA not available at compile time" << std::endl;
    return false;
#endif

    // Create parser
    if (!createParser()) {
        std::cerr << "NvdecDecoder: Failed to create video parser" << std::endl;
        cuCtxPopCurrent(nullptr);
        return false;
    }

    initialized_ = true;
    cuCtxPopCurrent(nullptr);
    return true;
}

bool NvdecDecoder::createParser() {
#ifdef HAVE_CUDA
    CUVIDPARSERPARAMS parserParams = {};
    parserParams.CodecType = (config_.codec == CodecType::H265) ? cudaVideoCodec_HEVC : cudaVideoCodec_H264;
    parserParams.ulMaxNumDecodeSurfaces = getSurfacePoolSize(config_.quality);
    parserParams.ulMaxDisplayDelay = 1;  // Low latency
    parserParams.pUserData = this;
    parserParams.pfnSequenceCallback = handleVideoSequence;
    parserParams.pfnDecodePicture = handlePictureDecode;
    parserParams.pfnDisplayPicture = handlePictureDisplay;

    CUresult result = cuvidCreateVideoParser(&parser_, &parserParams);
    if (result != CUDA_SUCCESS) {
        std::cerr << "NvdecDecoder: cuvidCreateVideoParser failed: " << result << std::endl;
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool NvdecDecoder::createDecoder(CUVIDEOFORMAT* format) {
#ifdef HAVE_CUDA
    destroyDecoder();
    freeSurfaces();

    // Set up decoder creation info
    std::memset(&decoderInfo_, 0, sizeof(decoderInfo_));
    decoderInfo_.CodecType = format->codec;
    decoderInfo_.ChromaFormat = format->chroma_format;
    decoderInfo_.OutputFormat = cudaVideoSurfaceFormat_NV12;
    decoderInfo_.bitDepthMinus8 = format->bit_depth_luma_minus8;

    decoderInfo_.ulWidth = format->coded_width;
    decoderInfo_.ulHeight = format->coded_height;
    decoderInfo_.ulMaxWidth = config_.maxWidth;
    decoderInfo_.ulMaxHeight = config_.maxHeight;

    decoderInfo_.ulNumDecodeSurfaces = getSurfacePoolSize(config_.quality);
    decoderInfo_.ulNumOutputSurfaces = 2;  // Double buffering for display
    decoderInfo_.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    decoderInfo_.ulIntraDecodeOnly = 0;

    decoderInfo_.vidLock = nullptr;  // We handle our own locking
    decoderInfo_.DeinterlaceMode = cudaVideoDeinterlaceMode_Adaptive;

    decoderInfo_.display_area.left = 0;
    decoderInfo_.display_area.top = 0;
    decoderInfo_.display_area.right = format->display_area.right;
    decoderInfo_.display_area.bottom = format->display_area.bottom;

    decoderInfo_.ulTargetWidth = format->display_area.right - format->display_area.left;
    decoderInfo_.ulTargetHeight = format->display_area.bottom - format->display_area.top;

    CUresult result = cuvidCreateDecoder(&decoder_, &decoderInfo_);
    if (result != CUDA_SUCCESS) {
        std::cerr << "NvdecDecoder: cuvidCreateDecoder failed: " << result << std::endl;
        return false;
    }

    // Allocate output surfaces
    if (!allocateSurfaces()) {
        destroyDecoder();
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool NvdecDecoder::allocateSurfaces() {
#ifdef HAVE_CUDA
    size_t numSurfaces = getSurfacePoolSize(config_.quality);
    surfaces_.resize(numSurfaces);

    size_t surfaceHeight = decoderInfo_.ulTargetHeight;
    // NV12 format: Y plane + UV plane (half height)
    size_t totalHeight = surfaceHeight + (surfaceHeight / 2);

    for (size_t i = 0; i < numSurfaces; ++i) {
        size_t pitch;
        CUresult result = cuMemAllocPitch(&surfaces_[i].devicePtr, &pitch,
                                          decoderInfo_.ulTargetWidth, totalHeight, 16);
        if (result != CUDA_SUCCESS) {
            std::cerr << "NvdecDecoder: Failed to allocate surface " << i << std::endl;
            // Free already allocated surfaces
            for (size_t j = 0; j < i; ++j) {
                cuMemFree(surfaces_[j].devicePtr);
            }
            surfaces_.clear();
            return false;
        }

        surfaces_[i].pitch = pitch;
        surfaces_[i].inUse = false;
        totalMemoryAllocated_ += pitch * totalHeight;
    }

    return true;
#else
    return false;
#endif
}

void NvdecDecoder::freeSurfaces() {
#ifdef HAVE_CUDA
    for (auto& surface : surfaces_) {
        if (surface.devicePtr) {
            cuMemFree(surface.devicePtr);
        }
    }
    surfaces_.clear();
    totalMemoryAllocated_ = 0;
#endif
}

void NvdecDecoder::destroyParser() {
#ifdef HAVE_CUDA
    if (parser_) {
        cuvidDestroyVideoParser(parser_);
        parser_ = nullptr;
    }
#endif
}

void NvdecDecoder::destroyDecoder() {
#ifdef HAVE_CUDA
    if (decoder_) {
        cuvidDestroyDecoder(decoder_);
        decoder_ = nullptr;
    }
#endif
}

DecodeResult NvdecDecoder::decode(const uint8_t* data, size_t size) {
    DecodeResult result = {};
    result.status = DecodeStatus::NEED_MORE_DATA;
    result.frame = nullptr;
    result.errorMessage = nullptr;

    if (!initialized_) {
        result.status = DecodeStatus::ERROR_DECODER_FAILURE;
        result.errorMessage = "Decoder not initialized";
        return result;
    }

#ifdef HAVE_CUDA
    // Set CUDA context
    cuCtxPushCurrent(cudaContext_);

    // Parse the bitstream
    CUVIDSOURCEDATAPACKET packet = {};
    packet.payload = data;
    packet.payload_size = size;
    packet.flags = CUVID_PKT_TIMESTAMP;
    packet.timestamp = 0;  // Will be set by parser callbacks

    CUresult cuResult = cuvidParseVideoData(parser_, &packet);

    cuCtxPopCurrent(nullptr);

    if (cuResult != CUDA_SUCCESS) {
        result.status = DecodeStatus::ERROR_DECODER_FAILURE;
        result.errorMessage = "cuvidParseVideoData failed";
        return result;
    }

    result.status = DecodeStatus::SUCCESS;
#else
    result.status = DecodeStatus::ERROR_DECODER_FAILURE;
    result.errorMessage = "CUDA not available";
#endif

    return result;
}

DecodedFrame* NvdecDecoder::getFrame() {
    std::lock_guard<std::mutex> lock(frameMutex_);

    if (frameQueue_.empty()) {
        return nullptr;
    }

    // Get the oldest frame from queue
    FrameInfo frameInfo = frameQueue_.front();
    frameQueue_.pop();

    // Map to DecodedFrame structure
    if (frameInfo.surfaceIndex >= 0 && frameInfo.surfaceIndex < static_cast<int>(surfaces_.size())) {
        Surface& surface = surfaces_[frameInfo.surfaceIndex];

        currentFrame_.cudaSurface = reinterpret_cast<void*>(surface.devicePtr);
        currentFrame_.cudaPitch = surface.pitch;
        currentFrame_.width = decoderInfo_.ulTargetWidth;
        currentFrame_.height = decoderInfo_.ulTargetHeight;
        currentFrame_.format = PixelFormat::NV12;
        currentFrame_.pts = frameInfo.pts;
        currentFrame_.isKeyframe = frameInfo.isKeyframe;

        // For NV12: Y plane at offset 0, UV plane at offset (height * pitch)
        currentFrame_.data[0] = reinterpret_cast<uint8_t*>(surface.devicePtr);
        currentFrame_.data[1] = reinterpret_cast<uint8_t*>(surface.devicePtr + (currentFrame_.height * surface.pitch));
        currentFrame_.data[2] = nullptr;  // NV12 has only 2 planes

        currentFrame_.pitch[0] = surface.pitch;
        currentFrame_.pitch[1] = surface.pitch;  // UV plane has same pitch
        currentFrame_.pitch[2] = 0;

        return &currentFrame_;
    }

    return nullptr;
}

void NvdecDecoder::setQuality(StreamQuality quality) {
    if (quality == config_.quality) {
        return;
    }

    config_.quality = quality;

    // Quality changes require re-allocating surface pool
    // This is safe to do because we're not actively decoding during quality change
    std::lock_guard<std::mutex> lock(frameMutex_);
    freeSurfaces();

    if (decoder_) {
        allocateSurfaces();
    }
}

MemoryStats NvdecDecoder::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    MemoryStats stats = {};
    stats.gpuMemoryUsed = totalMemoryAllocated_;
    stats.systemMemoryUsed = sizeof(*this) + surfaces_.size() * sizeof(Surface);
    stats.surfacePoolSize = surfaces_.size();
    stats.surfacePoolCapacity = getSurfacePoolSize(config_.quality);

    return stats;
}

void NvdecDecoder::flush() {
#ifdef HAVE_CUDA
    if (!parser_) {
        return;
    }

    cuCtxPushCurrent(cudaContext_);

    // Send flush packet
    CUVIDSOURCEDATAPACKET packet = {};
    packet.payload = nullptr;
    packet.payload_size = 0;
    packet.flags = CUVID_PKT_ENDOFSTREAM;

    cuvidParseVideoData(parser_, &packet);

    cuCtxPopCurrent(nullptr);
#endif
}

void NvdecDecoder::reset() {
    std::lock_guard<std::mutex> lock(frameMutex_);

    // Clear frame queue
    while (!frameQueue_.empty()) {
        frameQueue_.pop();
    }

    // Mark all surfaces as not in use
    for (auto& surface : surfaces_) {
        surface.inUse = false;
    }

    framesDecoded_ = 0;
}

// NVDEC Callbacks
int CUDAAPI NvdecDecoder::handleVideoSequence(void* userData, CUVIDEOFORMAT* format) {
    NvdecDecoder* decoder = static_cast<NvdecDecoder*>(userData);

    // Create decoder on first sequence or when format changes
    if (!decoder->decoder_ ||
        decoder->decoderInfo_.ulWidth != format->coded_width ||
        decoder->decoderInfo_.ulHeight != format->coded_height) {

        if (!decoder->createDecoder(format)) {
            return 0;  // Fail
        }
    }

    return getSurfacePoolSize(decoder->config_.quality);  // Return number of decode surfaces
}

int CUDAAPI NvdecDecoder::handlePictureDecode(void* userData, CUVIDPICPARAMS* picParams) {
#ifdef HAVE_CUDA
    NvdecDecoder* decoder = static_cast<NvdecDecoder*>(userData);

    if (!decoder->decoder_) {
        return 0;
    }

    CUresult result = cuvidDecodePicture(decoder->decoder_, picParams);
    if (result != CUDA_SUCCESS) {
        std::cerr << "NvdecDecoder: cuvidDecodePicture failed: " << result << std::endl;
        return 0;
    }

    return 1;  // Success
#else
    return 0;
#endif
}

int CUDAAPI NvdecDecoder::handlePictureDisplay(void* userData, CUVIDPARSERDISPINFO* dispInfo) {
#ifdef HAVE_CUDA
    NvdecDecoder* decoder = static_cast<NvdecDecoder*>(userData);

    // Map decoded picture to output surface
    CUVIDPROCPARAMS procParams = {};
    procParams.progressive_frame = !dispInfo->progressive_frame;
    procParams.second_field = dispInfo->repeat_first_field + 1;
    procParams.top_field_first = dispInfo->top_field_first;
    procParams.unpaired_field = (dispInfo->repeat_first_field < 0);

    CUdeviceptr decodedSurface = 0;
    unsigned int pitch = 0;
    CUresult result = cuvidMapVideoFrame(decoder->decoder_, dispInfo->picture_index,
                                         &decodedSurface, &pitch, &procParams);

    if (result != CUDA_SUCCESS) {
        std::cerr << "NvdecDecoder: cuvidMapVideoFrame failed: " << result << std::endl;
        return 0;
    }

    // Find available surface in our pool
    std::lock_guard<std::mutex> lock(decoder->frameMutex_);

    int surfaceIndex = -1;
    for (size_t i = 0; i < decoder->surfaces_.size(); ++i) {
        if (!decoder->surfaces_[i].inUse) {
            surfaceIndex = static_cast<int>(i);
            decoder->surfaces_[i].inUse = true;
            break;
        }
    }

    if (surfaceIndex >= 0) {
        // Copy from decoder surface to our surface pool
        Surface& surface = decoder->surfaces_[surfaceIndex];

        // Copy Y plane
        CUDA_MEMCPY2D copyParams = {};
        copyParams.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copyParams.srcDevice = decodedSurface;
        copyParams.srcPitch = pitch;
        copyParams.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copyParams.dstDevice = surface.devicePtr;
        copyParams.dstPitch = surface.pitch;
        copyParams.WidthInBytes = decoder->decoderInfo_.ulTargetWidth;
        copyParams.Height = decoder->decoderInfo_.ulTargetHeight;

        cuMemcpy2D(&copyParams);

        // Copy UV plane (NV12 interleaved)
        copyParams.srcDevice = decodedSurface + (pitch * decoder->decoderInfo_.ulTargetHeight);
        copyParams.dstDevice = surface.devicePtr + (surface.pitch * decoder->decoderInfo_.ulTargetHeight);
        copyParams.Height = decoder->decoderInfo_.ulTargetHeight / 2;

        cuMemcpy2D(&copyParams);

        // Add to frame queue
        FrameInfo frameInfo;
        frameInfo.surfaceIndex = surfaceIndex;
        frameInfo.pts = dispInfo->timestamp;
        frameInfo.isKeyframe = (dispInfo->picture_index == 0);  // Simplified keyframe detection

        decoder->frameQueue_.push(frameInfo);
        decoder->framesDecoded_++;
    }

    // Unmap the decoder surface
    cuvidUnmapVideoFrame(decoder->decoder_, decodedSurface);

    return 1;  // Success
#else
    return 0;
#endif
}

} // namespace fluxvision
