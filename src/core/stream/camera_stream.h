// src/core/stream/camera_stream.h
// Per-camera state encapsulation (network + decoder + queue)
#pragma once

#include "../network/rtsp_client.h"
#include "../codec/decoder_interface.h"
#include "../threading/bounded_queue.h"
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>

namespace fluxvision {
namespace stream {

// Quality levels for dynamic bitrate/resolution
enum class StreamQuality {
    PAUSED,       // Stream paused (no decoding, minimal network)
    THUMBNAIL,    // 160×90 @ 5fps (for small preview)
    GRID_VIEW,    // 640×360 @ 15fps (default grid view)
    FOCUSED,      // 1280×720 @ 30fps (single camera focus)
    FULLSCREEN    // 1920×1080 @ 30fps (fullscreen playback)
};

// Camera stream state
enum class StreamState {
    STOPPED,      // Not started
    CONNECTING,   // Attempting RTSP connection
    RUNNING,      // Active streaming and decoding
    ERROR,        // Encountered error
    RECONNECTING  // Auto-reconnecting after failure
};

// Packet for per-camera queue
struct StreamPacket {
    std::vector<uint8_t> data;
    int64_t timestamp;
    bool isKeyFrame;
};

// Per-camera stream manager
class CameraStream {
public:
    struct Config {
        std::string id;              // Unique camera identifier
        std::string rtspUrl;         // RTSP stream URL
        std::string username;        // RTSP auth username
        std::string password;        // RTSP auth password
        StreamQuality quality = StreamQuality::GRID_VIEW;
        bool autoReconnect = true;   // Auto-reconnect on failure
        size_t packetQueueSize = 60; // Bounded queue size (2 seconds @ 30fps)
    };

    struct Stats {
        int currentFps = 0;
        int droppedFrames = 0;
        int decodedFrames = 0;
        size_t packetsInQueue = 0;
        size_t bytesReceived = 0;
        int64_t lastFrameTimestamp = 0;
        std::chrono::milliseconds latency{0};
    };

    explicit CameraStream(const Config& config);
    ~CameraStream();

    // Delete copy/move
    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    // Lifecycle
    bool start();
    void stop();
    bool reconnect();

    // Quality control
    void setQuality(StreamQuality quality);
    StreamQuality getQuality() const { return quality_.load(); }

    // State
    StreamState getState() const { return state_.load(); }
    bool isRunning() const { return state_.load() == StreamState::RUNNING; }

    // Statistics
    Stats getStats() const;

    // Accessors for internal components (used by StreamManager)
    network::RtspClient* getRtspClient() { return rtspClient_.get(); }
    IDecoder* getDecoder() { return decoder_.get(); }
    threading::BoundedQueue<StreamPacket>* getPacketQueue() { return &packetQueue_; }

    // Configuration
    const Config& getConfig() const { return config_; }
    std::string getId() const { return config_.id; }

private:
    Config config_;
    std::atomic<StreamQuality> quality_;
    std::atomic<StreamState> state_{StreamState::STOPPED};

    // Core components
    std::unique_ptr<network::RtspClient> rtspClient_;
    std::unique_ptr<IDecoder> decoder_;
    threading::BoundedQueue<StreamPacket> packetQueue_;

    // Statistics tracking
    mutable std::mutex statsMutex_;
    Stats stats_;
    std::chrono::steady_clock::time_point lastFpsUpdate_;
    int framesSinceLastUpdate_ = 0;

    // Internal helpers
    bool initializeRtspClient();
    bool initializeDecoder();
    void updateState(StreamState newState);
    void updateFps();
};

} // namespace stream
} // namespace fluxvision
