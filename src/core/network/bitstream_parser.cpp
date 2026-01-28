#include "bitstream_parser.h"
#include "h264_parser.h"
#include <cstring>

namespace fluxvision {
namespace network {

int BitstreamParser::parsePacket(const uint8_t* data, size_t size, int64_t timestamp) {
    if (!data || size == 0) {
        return 0;
    }

    // Find all NAL start codes in the bitstream
    std::vector<size_t> startCodes = findStartCodes(data, size);

    if (startCodes.empty()) {
        // No start codes found - might be single NAL without start code
        // Or malformed data - skip it
        return 0;
    }

    int nalCount = 0;

    // Extract each NAL unit
    for (size_t i = 0; i < startCodes.size(); i++) {
        size_t nalStart = startCodes[i];
        size_t nalEnd = (i + 1 < startCodes.size()) ? startCodes[i + 1] : size;
        size_t nalSize = nalEnd - nalStart;

        if (nalSize > 0) {
            NalUnit nal = extractNalUnit(data + nalStart, nalSize, timestamp);
            if (nal.type != NalUnitType::UNSPECIFIED) {
                nalUnits_.push(std::move(nal));
                nalCount++;
            }
        }
    }

    return nalCount;
}

bool BitstreamParser::getNalUnit(NalUnit& nal) {
    if (nalUnits_.empty()) {
        return false;
    }

    nal = std::move(nalUnits_.front());
    nalUnits_.pop();
    return true;
}

bool BitstreamParser::hasNalUnits() const {
    return !nalUnits_.empty();
}

void BitstreamParser::reset() {
    while (!nalUnits_.empty()) {
        nalUnits_.pop();
    }
}

std::vector<size_t> BitstreamParser::findStartCodes(const uint8_t* data, size_t size) {
    std::vector<size_t> positions;

    if (size < 3) {
        return positions;
    }

    for (size_t i = 0; i < size - 2; i++) {
        // Check for 3-byte start code: 0x000001
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            positions.push_back(i);
            i += 2;  // Skip past this start code
        }
        // Check for 4-byte start code: 0x00000001
        else if (i < size - 3 &&
                 data[i] == 0x00 && data[i + 1] == 0x00 &&
                 data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            positions.push_back(i);
            i += 3;  // Skip past this start code
        }
    }

    return positions;
}

NalUnit BitstreamParser::extractNalUnit(const uint8_t* data, size_t size, int64_t timestamp) {
    NalUnit nal;
    nal.pts = timestamp;
    nal.dts = timestamp;

    if (!data || size == 0) {
        return nal;
    }

    // Copy the entire NAL unit including start code
    nal.data.resize(size);
    std::memcpy(nal.data.data(), data, size);

    // Parse NAL header to get type
    size_t nalSize = 0;
    const uint8_t* nalHeader = H264Parser::skipStartCode(data, size, nalSize);

    if (nalHeader && nalSize > 0) {
        nal.type = H264Parser::getNalType(nalHeader, nalSize);
        nal.isKeyframe = H264Parser::isKeyframe(nalHeader, nalSize);

        // For SPS, extract resolution info
        if (nal.type == NalUnitType::SPS) {
            SPSInfo sps;
            if (H264Parser::extractSPS(data, size, sps)) {
                nal.width = sps.width;
                nal.height = sps.height;
                nal.framerate = sps.framerate;
            }
        }
    }

    return nal;
}

} // namespace network
} // namespace fluxvision
