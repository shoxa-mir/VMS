#pragma once

#include "types.h"
#include <cstdint>

namespace fluxvision {
namespace network {

/**
 * H.264 NAL Unit Parser
 *
 * Parses H.264 NAL units to extract:
 * - NAL unit type and metadata
 * - SPS (Sequence Parameter Set) for resolution and framerate
 * - PPS (Picture Parameter Set) for encoding settings
 * - Frame type detection (I, P, B frames)
 *
 * Thread-safety: All methods are thread-safe (stateless)
 */
class H264Parser {
public:
    H264Parser() = default;
    ~H264Parser() = default;

    struct NalInfo {
        NalUnitType type = NalUnitType::UNSPECIFIED;
        bool isKeyframe = false;
        int refIdc = 0;  // nal_ref_idc (0-3)
    };

    /**
     * Parse NAL unit header to get basic info
     */
    static NalInfo parseNalHeader(const uint8_t* data, size_t size);

    /**
     * Extract SPS information
     * Returns: true if SPS parsed successfully
     */
    static bool extractSPS(const uint8_t* data, size_t size, SPSInfo& sps);

    /**
     * Extract PPS information
     * Returns: true if PPS parsed successfully
     */
    static bool extractPPS(const uint8_t* data, size_t size, PPSInfo& pps);

    /**
     * Check if NAL unit is a keyframe (IDR, SPS, or PPS)
     */
    static bool isKeyframe(const uint8_t* data, size_t size);

    /**
     * Get NAL unit type from header
     */
    static NalUnitType getNalType(const uint8_t* data, size_t size);

    /**
     * Check if NAL unit starts with start code (0x000001 or 0x00000001)
     */
    static bool hasStartCode(const uint8_t* data, size_t size);

    /**
     * Remove start code and return pointer to NAL header
     * Returns: pointer to NAL header, or nullptr if no start code
     */
    static const uint8_t* skipStartCode(const uint8_t* data, size_t size, size_t& nalSize);

private:
    // Exponential Golomb decoding for SPS/PPS
    class BitReader {
    public:
        BitReader(const uint8_t* data, size_t size);

        uint32_t readBits(int numBits);
        uint32_t readUE();  // Read unsigned exponential-Golomb code
        int32_t readSE();   // Read signed exponential-Golomb code
        bool hasMoreData() const;

    private:
        const uint8_t* data_;
        size_t size_;
        size_t bytePos_ = 0;
        int bitPos_ = 0;
    };

    static bool parseSPS_Simple(BitReader& reader, SPSInfo& sps);
};

} // namespace network
} // namespace fluxvision
