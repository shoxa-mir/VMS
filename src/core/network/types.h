#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace fluxvision {
namespace network {

/**
 * Transport protocol for RTSP streaming
 */
enum class TransportType {
    TCP,    // Interleaved RTP over TCP (preferred for reliability)
    UDP     // RTP over UDP (lower latency but packet loss possible)
};

/**
 * Connection state tracking
 */
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
    RECONNECTING
};

/**
 * NAL unit types for H.264/H.265
 */
enum class NalUnitType : uint8_t {
    // H.264 NAL types
    UNSPECIFIED = 0,
    SLICE = 1,          // Non-IDR picture
    DPA = 2,            // Data partition A
    DPB = 3,            // Data partition B
    DPC = 4,            // Data partition C
    IDR = 5,            // IDR picture (keyframe)
    SEI = 6,            // Supplemental enhancement information
    SPS = 7,            // Sequence parameter set
    PPS = 8,            // Picture parameter set
    AUD = 9,            // Access unit delimiter
    END_SEQUENCE = 10,
    END_STREAM = 11,
    FILLER = 12,

    // FU-A fragmentation (for RTP)
    FU_A = 28,
    FU_B = 29,

    // H.265 NAL types (for future support)
    HEVC_VPS = 32,      // Video parameter set
    HEVC_SPS = 33,
    HEVC_PPS = 34,
    HEVC_IDR_W_RADL = 19,
    HEVC_IDR_N_LP = 20
};

/**
 * Stream profile (main vs sub stream)
 */
enum class StreamProfile {
    MAIN,       // High resolution main stream (1080p, 4MP, etc.)
    SUB         // Low resolution sub stream (D1, 720p)
};

/**
 * RTP packet structure
 */
struct RtpPacket {
    uint16_t sequenceNumber = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    uint8_t payloadType = 0;
    bool marker = false;

    std::vector<uint8_t> payload;

    // Metadata
    int64_t receiveTime = 0;  // microseconds
};

/**
 * NAL unit with metadata
 */
struct NalUnit {
    NalUnitType type = NalUnitType::UNSPECIFIED;
    std::vector<uint8_t> data;

    // Metadata
    int64_t pts = 0;           // Presentation timestamp (microseconds)
    int64_t dts = 0;           // Decode timestamp (microseconds)
    bool isKeyframe = false;
    StreamProfile profile = StreamProfile::MAIN;

    // SPS/PPS info (if parsed)
    int width = 0;
    int height = 0;
    int framerate = 0;
};

/**
 * SPS (Sequence Parameter Set) parsed information
 */
struct SPSInfo {
    int width = 0;
    int height = 0;
    int framerate = 0;
    int profile = 0;
    int level = 0;
    bool interlaced = false;
};

/**
 * PPS (Picture Parameter Set) parsed information
 */
struct PPSInfo {
    int ppsId = 0;
    int spsId = 0;
    bool entropyCodingMode = false;  // CABAC vs CAVLC
};

/**
 * Connection statistics
 */
struct NetworkStats {
    uint64_t packetsReceived = 0;
    uint64_t packetsLost = 0;
    uint64_t bytesReceived = 0;

    double packetLossRate = 0.0;
    double bitrate = 0.0;           // Mbps
    int64_t latency = 0;            // microseconds

    int reconnectCount = 0;
    int64_t uptime = 0;             // seconds
};

} // namespace network
} // namespace fluxvision
