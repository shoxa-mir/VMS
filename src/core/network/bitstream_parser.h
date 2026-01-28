#pragma once

#include "types.h"
#include <vector>
#include <queue>

namespace fluxvision {
namespace network {

/**
 * H.264 Bitstream Parser
 *
 * Parses H.264 bitstream (from FFmpeg av_read_frame) into individual NAL units.
 * FFmpeg already handles RTSP/RTP depacketization, so we just need to split
 * the bitstream by NAL start codes.
 */
class BitstreamParser {
public:
    BitstreamParser() = default;
    ~BitstreamParser() = default;

    /**
     * Parse H.264 bitstream packet into NAL units
     * @param data Bitstream data (may contain multiple NAL units)
     * @param size Data size
     * @param timestamp PTS from AVPacket
     * @return Number of NAL units extracted
     */
    int parsePacket(const uint8_t* data, size_t size, int64_t timestamp);

    /**
     * Get next NAL unit
     */
    bool getNalUnit(NalUnit& nal);

    /**
     * Check if NAL units available
     */
    bool hasNalUnits() const;

    /**
     * Clear all queued NAL units
     */
    void reset();

private:
    std::queue<NalUnit> nalUnits_;

    /**
     * Find NAL start code positions in bitstream
     * @return Vector of start code positions
     */
    std::vector<size_t> findStartCodes(const uint8_t* data, size_t size);

    /**
     * Extract single NAL unit and determine type
     */
    NalUnit extractNalUnit(const uint8_t* data, size_t size, int64_t timestamp);
};

} // namespace network
} // namespace fluxvision
