#pragma once

#include "types.h"
#include <queue>
#include <mutex>
#include <vector>

namespace fluxvision {
namespace network {

/**
 * RTP Depacketizer for H.264/H.265
 *
 * Converts RTP packets to complete NAL units, handling:
 * - Single NAL unit packets
 * - Fragmentation units (FU-A) for large NAL units
 * - Packet reordering
 * - Packet loss detection
 *
 * Thread-safety: All methods are thread-safe
 */
class RtpDepacketizer {
public:
    RtpDepacketizer();
    ~RtpDepacketizer() = default;

    /**
     * Add RTP packet for processing
     * Returns: true on success, false if packet is invalid or lost
     */
    bool addPacket(const RtpPacket& packet);

    /**
     * Get next complete NAL unit (non-blocking)
     * Returns: true if NAL unit available, false if queue empty
     */
    bool getNalUnit(NalUnit& nalUnit);

    /**
     * Check if NAL units are available
     */
    bool hasNalUnits() const;

    /**
     * Get number of NAL units in queue
     */
    size_t getNalUnitCount() const;

    /**
     * Reset depacketizer state
     */
    void reset();

    /**
     * Get depacketizer statistics
     */
    struct Stats {
        uint64_t packetsProcessed = 0;
        uint64_t nalUnitsExtracted = 0;
        uint64_t fragmentedNalUnits = 0;
        uint64_t packetsLost = 0;
        uint64_t packetsOutOfOrder = 0;
    };

    Stats getStats() const;

private:
    struct RtpHeader {
        uint8_t version;
        bool padding;
        bool extension;
        uint8_t csrcCount;
        bool marker;
        uint8_t payloadType;
        uint16_t sequenceNumber;
        uint32_t timestamp;
        uint32_t ssrc;
    };

    bool parseRtpHeader(const std::vector<uint8_t>& data, RtpHeader& header, size_t& headerSize);
    bool processSingleNalUnit(const uint8_t* payload, size_t size, uint32_t timestamp);
    bool processFragmentedNalUnit(const uint8_t* payload, size_t size, uint32_t timestamp);

    void completeNalUnit(uint32_t timestamp);
    NalUnitType getNalType(uint8_t header) const;
    bool isKeyframe(NalUnitType type) const;

    mutable std::mutex mutex_;
    std::queue<NalUnit> nalUnits_;

    // Fragmentation state
    std::vector<uint8_t> fragmentBuffer_;
    uint32_t fragmentTimestamp_ = 0;
    bool fragmentInProgress_ = false;

    // Packet tracking
    uint16_t lastSequenceNumber_ = 0;
    bool firstPacket_ = true;

    // Statistics
    Stats stats_;
};

} // namespace network
} // namespace fluxvision
