#pragma once

#include "types.h"
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

// Forward declarations for FFmpeg
struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;

namespace fluxvision {
namespace network {

/**
 * RTSP Client using FFmpeg libavformat
 *
 * Lightweight RTSP client that avoids GStreamer's per-camera pipeline overhead.
 * Uses FFmpeg's libavformat for RTSP negotiation and RTP packet reception.
 *
 * Features:
 * - TCP transport (reliable, firewall-friendly)
 * - Dual-stream support (main + sub)
 * - Automatic reconnection
 * - Low overhead (single thread per camera)
 *
 * Thread-safety: All methods are thread-safe
 */
class RtspClient {
public:
    struct Config {
        std::string url;
        std::string username;
        std::string password;
        TransportType transport = TransportType::TCP;
        int timeoutMs = 5000;
        bool enableSubStream = true;

        // Reconnection settings
        bool autoReconnect = true;
        int maxReconnectAttempts = 10;
        int reconnectDelayMs = 3000;

        // Buffer settings
        int receiveBufferSize = 2 * 1024 * 1024;  // 2MB
        bool lowLatency = true;  // Minimize buffering
    };

    /**
     * Packet callback: called when RTP packet is received
     * Return false to stop receiving
     */
    using PacketCallback = std::function<bool(const RtpPacket& packet)>;

    RtspClient();
    ~RtspClient();

    // Disable copy
    RtspClient(const RtspClient&) = delete;
    RtspClient& operator=(const RtspClient&) = delete;

    /**
     * Connect to RTSP camera
     * Returns: true on success, false on failure
     */
    bool connect(const Config& config);

    /**
     * Disconnect from camera
     */
    void disconnect();

    /**
     * Receive next RTP packet (blocking)
     * Returns: true if packet received, false on error or timeout
     * Note: FFmpeg already depacketizes RTP, this returns H.264 bitstream
     */
    bool receivePacket(RtpPacket& packet);

    /**
     * Receive NAL units directly (blocking)
     * FFmpeg handles RTSP/RTP, we parse the H.264 bitstream
     * Returns: Number of NAL units received (0 on timeout/error)
     */
    int receiveNalUnits(std::vector<NalUnit>& nalUnits);

    /**
     * Start receiving packets asynchronously with callback
     * Returns: true if started successfully
     */
    bool startReceiving(PacketCallback callback);

    /**
     * Stop async receiving
     */
    void stopReceiving();

    /**
     * Switch between main and sub streams
     */
    bool switchToMainStream();
    bool switchToSubStream();

    /**
     * Get current connection state
     */
    ConnectionState getState() const;

    /**
     * Get stream information
     */
    bool getStreamInfo(int& width, int& height, int& framerate) const;

    /**
     * Extract SPS/PPS from codec extradata (from RTSP SDP)
     * These are sent out-of-band during RTSP negotiation
     * Returns: true if extradata contains SPS/PPS
     */
    bool getExtradata(std::vector<NalUnit>& nalUnits) const;

    /**
     * Get network statistics
     */
    NetworkStats getStats() const;

    /**
     * Get current profile
     */
    StreamProfile getCurrentProfile() const { return currentProfile_; }

private:
    bool openStream(const std::string& url);
    void closeStream();
    bool parseRtpPacket(AVPacket* avPacket, RtpPacket& packet);
    void updateStats(const RtpPacket& packet);

    // Reconnection logic
    bool attemptReconnect();
    void reconnectLoop();

    Config config_;
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecParameters* codecParams_ = nullptr;

    mutable std::mutex mutex_;
    ConnectionState state_ = ConnectionState::DISCONNECTED;
    StreamProfile currentProfile_ = StreamProfile::MAIN;

    // Statistics
    NetworkStats stats_;
    int64_t lastPacketTime_ = 0;
    uint16_t lastSeqNumber_ = 0;
    int64_t startTime_ = 0;

    // Async receiving
    std::atomic<bool> receiving_{false};
    std::unique_ptr<std::thread> receiveThread_;
    PacketCallback packetCallback_;
};

} // namespace network
} // namespace fluxvision
