#include "rtsp_client.h"
#include "bitstream_parser.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
}

namespace fluxvision {
namespace network {

namespace {
    int64_t getCurrentTimeMicros() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
}

RtspClient::RtspClient() {
    // Initialize FFmpeg network (thread-safe, can be called multiple times)
    avformat_network_init();
}

RtspClient::~RtspClient() {
    disconnect();
}

bool RtspClient::connect(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == ConnectionState::CONNECTED) {
        std::cerr << "RtspClient: Already connected" << std::endl;
        return true;
    }

    config_ = config;
    state_ = ConnectionState::CONNECTING;

    if (!openStream(config.url)) {
        state_ = ConnectionState::ERROR;
        return false;
    }

    state_ = ConnectionState::CONNECTED;
    startTime_ = getCurrentTimeMicros();
    stats_ = NetworkStats{};

    std::cout << "RtspClient: Connected to " << config.url << std::endl;
    return true;
}

void RtspClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    stopReceiving();
    closeStream();

    state_ = ConnectionState::DISCONNECTED;
    std::cout << "RtspClient: Disconnected" << std::endl;
}

bool RtspClient::openStream(const std::string& url) {
    AVDictionary* options = nullptr;

    // Set RTSP options for low latency and reliability
    av_dict_set(&options, "rtsp_transport",
                config_.transport == TransportType::TCP ? "tcp" : "udp", 0);
    av_dict_set(&options, "stimeout", std::to_string(config_.timeoutMs * 1000).c_str(), 0);
    av_dict_set(&options, "max_delay", "500000", 0);  // 500ms max delay

    if (config_.lowLatency) {
        av_dict_set(&options, "fflags", "nobuffer", 0);
        av_dict_set(&options, "flags", "low_delay", 0);
        av_dict_set(&options, "rtsp_flags", "prefer_tcp", 0);
    }

    // Set buffer size
    av_dict_set(&options, "buffer_size", std::to_string(config_.receiveBufferSize).c_str(), 0);

    // Open RTSP stream
    formatCtx_ = avformat_alloc_context();
    if (!formatCtx_) {
        std::cerr << "RtspClient: Failed to allocate format context" << std::endl;
        av_dict_free(&options);
        return false;
    }

    // Set timeout callback to prevent infinite hangs
    formatCtx_->interrupt_callback.callback = [](void*) -> int {
        // TODO: Implement proper timeout logic
        return 0;
    };

    int ret = avformat_open_input(&formatCtx_, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "RtspClient: Failed to open stream: " << errbuf << std::endl;
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }

    // Find stream information
    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0) {
        std::cerr << "RtspClient: Failed to find stream info" << std::endl;
        closeStream();
        return false;
    }

    // Find video stream
    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            codecParams_ = formatCtx_->streams[i]->codecpar;
            break;
        }
    }

    if (!codecParams_) {
        std::cerr << "RtspClient: No video stream found" << std::endl;
        closeStream();
        return false;
    }

    // Log stream info
    std::cout << "RtspClient: Video stream found - "
              << codecParams_->width << "x" << codecParams_->height
              << " codec: " << avcodec_get_name(codecParams_->codec_id) << std::endl;

    return true;
}

void RtspClient::closeStream() {
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
        codecParams_ = nullptr;
    }
}

bool RtspClient::receivePacket(RtpPacket& packet) {
    if (!formatCtx_ || state_ != ConnectionState::CONNECTED) {
        return false;
    }

    AVPacket* avPacket = av_packet_alloc();
    if (!avPacket) {
        return false;
    }

    int ret = av_read_frame(formatCtx_, avPacket);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            av_packet_free(&avPacket);
            return false;  // No packet available, try again
        }

        // Connection error
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "RtspClient: Read error: " << errbuf << std::endl;

        av_packet_free(&avPacket);

        if (config_.autoReconnect) {
            state_ = ConnectionState::RECONNECTING;
            std::thread([this] { attemptReconnect(); }).detach();
        } else {
            state_ = ConnectionState::ERROR;
        }

        return false;
    }

    // Parse RTP packet
    bool success = parseRtpPacket(avPacket, packet);
    av_packet_free(&avPacket);

    if (success) {
        updateStats(packet);
    }

    return success;
}

bool RtspClient::parseRtpPacket(AVPacket* avPacket, RtpPacket& packet) {
    if (!avPacket || avPacket->size <= 0) {
        return false;
    }

    // Copy payload data
    packet.payload.resize(avPacket->size);
    std::memcpy(packet.payload.data(), avPacket->data, avPacket->size);

    // Set timestamps
    packet.timestamp = avPacket->pts != AV_NOPTS_VALUE ? avPacket->pts : avPacket->dts;
    packet.receiveTime = getCurrentTimeMicros();

    // RTP metadata (simplified - full RTP header parsing done in depacketizer)
    packet.sequenceNumber = ++lastSeqNumber_;
    packet.marker = (avPacket->flags & AV_PKT_FLAG_KEY) != 0;

    return true;
}

int RtspClient::receiveNalUnits(std::vector<NalUnit>& nalUnits) {
    if (!formatCtx_ || state_ != ConnectionState::CONNECTED) {
        return 0;
    }

    nalUnits.clear();

    AVPacket* avPacket = av_packet_alloc();
    if (!avPacket) {
        return 0;
    }

    int ret = av_read_frame(formatCtx_, avPacket);
    if (ret < 0) {
        av_packet_free(&avPacket);

        if (ret != AVERROR(EAGAIN)) {
            // Connection error
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "RtspClient: Read error: " << errbuf << std::endl;

            if (config_.autoReconnect) {
                state_ = ConnectionState::RECONNECTING;
                std::thread([this] { attemptReconnect(); }).detach();
            } else {
                state_ = ConnectionState::ERROR;
            }
        }

        return 0;
    }

    // FFmpeg gives us H.264 bitstream (already RTP-depacketized)
    // Parse it into individual NAL units
    BitstreamParser parser;
    int nalCount = parser.parsePacket(avPacket->data, avPacket->size,
                                      avPacket->pts != AV_NOPTS_VALUE ? avPacket->pts : avPacket->dts);

    // Extract all NAL units
    NalUnit nal;
    while (parser.getNalUnit(nal)) {
        nalUnits.push_back(std::move(nal));
    }

    // Update stats (simplified - count bytes)
    stats_.packetsReceived++;
    stats_.bytesReceived += avPacket->size;

    int64_t now = getCurrentTimeMicros();
    if (lastPacketTime_ > 0) {
        int64_t timeDiff = now - lastPacketTime_;
        if (timeDiff > 0) {
            double bitsPerSecond = (avPacket->size * 8.0) / (timeDiff / 1000000.0);
            stats_.bitrate = stats_.bitrate * 0.9 + (bitsPerSecond / 1000000.0) * 0.1;
        }
    }
    lastPacketTime_ = now;
    stats_.uptime = (now - startTime_) / 1000000;

    av_packet_free(&avPacket);

    return nalCount;
}

void RtspClient::updateStats(const RtpPacket& packet) {
    stats_.packetsReceived++;
    stats_.bytesReceived += packet.payload.size();

    int64_t now = getCurrentTimeMicros();

    // Calculate bitrate (rolling average over last second)
    if (lastPacketTime_ > 0) {
        int64_t timeDiff = now - lastPacketTime_;
        if (timeDiff > 0) {
            double bitsPerSecond = (packet.payload.size() * 8.0) / (timeDiff / 1000000.0);
            stats_.bitrate = stats_.bitrate * 0.9 + (bitsPerSecond / 1000000.0) * 0.1;  // Mbps
        }
    }

    lastPacketTime_ = now;

    // Calculate uptime
    stats_.uptime = (now - startTime_) / 1000000;  // seconds

    // Packet loss detection (simplified)
    if (lastSeqNumber_ > 0) {
        uint16_t expected = lastSeqNumber_ + 1;
        if (packet.sequenceNumber != expected && packet.sequenceNumber > expected) {
            stats_.packetsLost += (packet.sequenceNumber - expected);
        }
    }

    if (stats_.packetsReceived > 0) {
        stats_.packetLossRate = static_cast<double>(stats_.packetsLost) /
                               (stats_.packetsReceived + stats_.packetsLost) * 100.0;
    }
}

bool RtspClient::startReceiving(PacketCallback callback) {
    if (!callback || state_ != ConnectionState::CONNECTED) {
        return false;
    }

    stopReceiving();

    packetCallback_ = callback;
    receiving_ = true;

    receiveThread_ = std::make_unique<std::thread>([this] {
        RtpPacket packet;
        while (receiving_) {
            if (receivePacket(packet)) {
                if (!packetCallback_(packet)) {
                    break;  // Callback requested stop
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    return true;
}

void RtspClient::stopReceiving() {
    if (receiving_) {
        receiving_ = false;
        if (receiveThread_ && receiveThread_->joinable()) {
            receiveThread_->join();
        }
        receiveThread_.reset();
    }
}

bool RtspClient::switchToMainStream() {
    // TODO: Implement stream switching by reconnecting with different URL
    std::cerr << "RtspClient: Stream switching not yet implemented" << std::endl;
    return false;
}

bool RtspClient::switchToSubStream() {
    // TODO: Implement stream switching
    std::cerr << "RtspClient: Stream switching not yet implemented" << std::endl;
    return false;
}

ConnectionState RtspClient::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool RtspClient::getStreamInfo(int& width, int& height, int& framerate) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!codecParams_) {
        return false;
    }

    width = codecParams_->width;
    height = codecParams_->height;

    // Calculate framerate from stream
    if (formatCtx_ && formatCtx_->nb_streams > 0) {
        AVRational fps = formatCtx_->streams[0]->avg_frame_rate;
        if (fps.num > 0 && fps.den > 0) {
            framerate = fps.num / fps.den;
        } else {
            framerate = 25;  // Default
        }
    }

    return true;
}

bool RtspClient::getExtradata(std::vector<NalUnit>& nalUnits) const {
    std::lock_guard<std::mutex> lock(mutex_);

    nalUnits.clear();

    if (!codecParams_ || !codecParams_->extradata || codecParams_->extradata_size == 0) {
        return false;
    }

    // H.264 extradata is in AVCDecoderConfigurationRecord format (ISO/IEC 14496-15)
    // Parse it to extract SPS and PPS NAL units
    const uint8_t* data = codecParams_->extradata;
    int size = codecParams_->extradata_size;

    if (size < 7) {
        return false;  // Too small to be valid
    }

    // Check for AVCDecoderConfigurationRecord
    if (data[0] != 1) {
        // Not in avcC format, might be raw Annex B with start codes
        // Try parsing as bitstream
        BitstreamParser parser;
        parser.parsePacket(data, size, 0);

        NalUnit nal;
        while (parser.getNalUnit(nal)) {
            nalUnits.push_back(std::move(nal));
        }

        return !nalUnits.empty();
    }

    // Parse AVCDecoderConfigurationRecord
    int offset = 5;  // Skip configurationVersion, AVCProfileIndication, profile_compatibility, AVCLevelIndication, lengthSizeMinusOne

    // Number of SPS
    int numSPS = data[offset++] & 0x1F;

    for (int i = 0; i < numSPS; i++) {
        if (offset + 2 > size) break;

        int spsSize = (data[offset] << 8) | data[offset + 1];
        offset += 2;

        if (offset + spsSize > size) break;

        // Create NAL unit with start code
        NalUnit sps;
        sps.type = NalUnitType::SPS;
        sps.isKeyframe = true;
        sps.data.resize(spsSize + 4);
        sps.data[0] = 0x00;
        sps.data[1] = 0x00;
        sps.data[2] = 0x00;
        sps.data[3] = 0x01;
        std::memcpy(sps.data.data() + 4, data + offset, spsSize);

        nalUnits.push_back(std::move(sps));
        offset += spsSize;
    }

    // Number of PPS
    if (offset >= size) return !nalUnits.empty();

    int numPPS = data[offset++];

    for (int i = 0; i < numPPS; i++) {
        if (offset + 2 > size) break;

        int ppsSize = (data[offset] << 8) | data[offset + 1];
        offset += 2;

        if (offset + ppsSize > size) break;

        // Create NAL unit with start code
        NalUnit pps;
        pps.type = NalUnitType::PPS;
        pps.isKeyframe = true;
        pps.data.resize(ppsSize + 4);
        pps.data[0] = 0x00;
        pps.data[1] = 0x00;
        pps.data[2] = 0x00;
        pps.data[3] = 0x01;
        std::memcpy(pps.data.data() + 4, data + offset, ppsSize);

        nalUnits.push_back(std::move(pps));
        offset += ppsSize;
    }

    return !nalUnits.empty();
}

NetworkStats RtspClient::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool RtspClient::attemptReconnect() {
    for (int attempt = 0; attempt < config_.maxReconnectAttempts; attempt++) {
        std::cout << "RtspClient: Reconnection attempt " << (attempt + 1)
                  << "/" << config_.maxReconnectAttempts << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnectDelayMs));

        closeStream();

        if (openStream(config_.url)) {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = ConnectionState::CONNECTED;
            stats_.reconnectCount++;
            std::cout << "RtspClient: Reconnected successfully" << std::endl;
            return true;
        }
    }

    std::cerr << "RtspClient: Reconnection failed after "
              << config_.maxReconnectAttempts << " attempts" << std::endl;

    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ConnectionState::ERROR;
    return false;
}

} // namespace network
} // namespace fluxvision
