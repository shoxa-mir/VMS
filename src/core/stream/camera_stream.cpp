// src/core/stream/camera_stream.cpp
#include "camera_stream.h"
#include "../codec/decoder_factory.h"
#include <iostream>

namespace fluxvision {
namespace stream {

CameraStream::CameraStream(const Config& config)
    : config_(config)
    , quality_(config.quality)
    , packetQueue_(config.packetQueueSize)
    , lastFpsUpdate_(std::chrono::steady_clock::now())
{
}

CameraStream::~CameraStream() {
    stop();
}

bool CameraStream::start() {
    if (state_.load() == StreamState::RUNNING) {
        return true;  // Already running
    }

    updateState(StreamState::CONNECTING);

    // Initialize RTSP client
    if (!initializeRtspClient()) {
        updateState(StreamState::ERROR);
        return false;
    }

    // Initialize decoder
    if (!initializeDecoder()) {
        updateState(StreamState::ERROR);
        return false;
    }

    updateState(StreamState::RUNNING);
    return true;
}

void CameraStream::stop() {
    if (state_.load() == StreamState::STOPPED) {
        return;
    }

    updateState(StreamState::STOPPED);

    // Cleanup components
    if (rtspClient_) {
        rtspClient_->disconnect();
        rtspClient_.reset();
    }

    if (decoder_) {
        decoder_.reset();
    }

    // Clear packet queue
    StreamPacket dummy;
    while (packetQueue_.pop(dummy)) {
        // Drain queue
    }

    // Reset statistics
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = Stats{};
    framesSinceLastUpdate_ = 0;
}

bool CameraStream::reconnect() {
    if (state_.load() == StreamState::RECONNECTING) {
        return false;  // Already reconnecting
    }

    updateState(StreamState::RECONNECTING);

    // Stop current connection
    stop();

    // Wait brief moment before reconnecting
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Attempt to start again
    return start();
}

void CameraStream::setQuality(StreamQuality quality) {
    if (quality_.load() == quality) {
        return;  // No change
    }

    quality_.store(quality);

    // For PAUSED quality, we can pause decoding but keep network connection
    // For other qualities, decoder may need reinitialization with new resolution
    // (Implementation detail: this will be handled in Phase 4 when we add dynamic quality)
}

CameraStream::Stats CameraStream::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    Stats statsCopy = stats_;
    statsCopy.packetsInQueue = packetQueue_.size();
    return statsCopy;
}

bool CameraStream::initializeRtspClient() {
    try {
        network::RtspClient::Config rtspConfig;
        rtspConfig.url = config_.rtspUrl;
        rtspConfig.username = config_.username;
        rtspConfig.password = config_.password;
        rtspConfig.transport = network::TransportType::TCP;
        rtspConfig.timeoutMs = 5000;
        rtspConfig.autoReconnect = config_.autoReconnect;

        rtspClient_ = std::make_unique<network::RtspClient>();

        if (!rtspClient_->connect(rtspConfig)) {
            std::cerr << "Failed to connect RTSP client for camera: " << config_.id << std::endl;
            return false;
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception initializing RTSP client for camera " << config_.id
                  << ": " << e.what() << std::endl;
        return false;
    }
}

bool CameraStream::initializeDecoder() {
    try {
        // Get stream info from RTSP client
        int width, height, framerate;
        if (!rtspClient_->getStreamInfo(width, height, framerate)) {
            std::cerr << "Failed to get stream info for camera: " << config_.id << std::endl;
            return false;
        }

        // Create decoder configuration based on quality level
        DecoderConfig decoderConfig;
        decoderConfig.codec = CodecType::H264;  // Assume H.264 for now
        decoderConfig.quality = static_cast<fluxvision::StreamQuality>(quality_.load());
        decoderConfig.maxWidth = width;
        decoderConfig.maxHeight = height;
        decoderConfig.preferHardware = true;

        // Determine if this should use sub-stream resolution
        stream::StreamQuality quality = quality_.load();
        decoderConfig.isSubStream = (quality == stream::StreamQuality::THUMBNAIL ||
                                     quality == stream::StreamQuality::GRID_VIEW);

        // Try NVDEC first, fallback to CPU decoder
        decoder_ = DecoderFactory::create(DecoderType::NVDEC, decoderConfig);

        if (!decoder_) {
            std::cerr << "Failed to create NVDEC decoder for camera " << config_.id
                      << ", trying CPU decoder..." << std::endl;
            decoder_ = DecoderFactory::create(DecoderType::CPU, decoderConfig);
        }

        if (!decoder_) {
            std::cerr << "Failed to create any decoder for camera: " << config_.id << std::endl;
            return false;
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception initializing decoder for camera " << config_.id
                  << ": " << e.what() << std::endl;
        return false;
    }
}

void CameraStream::updateState(StreamState newState) {
    StreamState oldState = state_.exchange(newState);

    if (oldState != newState) {
        // State transition logging (can be disabled in production)
        const char* stateNames[] = {"STOPPED", "CONNECTING", "RUNNING", "ERROR", "RECONNECTING"};
        std::cout << "Camera " << config_.id << " state: "
                  << stateNames[static_cast<int>(oldState)] << " -> "
                  << stateNames[static_cast<int>(newState)] << std::endl;
    }
}

void CameraStream::updateFps() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdate_);

    if (elapsed.count() >= 1000) {  // Update every second
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.currentFps = static_cast<int>((framesSinceLastUpdate_ * 1000.0) / elapsed.count());
        framesSinceLastUpdate_ = 0;
        lastFpsUpdate_ = now;
    }
}

} // namespace stream
} // namespace fluxvision
