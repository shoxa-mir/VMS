#include "rtp_depacketizer.h"
#include <iostream>
#include <cstring>

namespace fluxvision {
namespace network {

RtpDepacketizer::RtpDepacketizer() {
    fragmentBuffer_.reserve(256 * 1024);  // Reserve 256KB for fragments
}

bool RtpDepacketizer::addPacket(const RtpPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (packet.payload.empty()) {
        return false;
    }

    // Check sequence number for packet loss
    if (!firstPacket_) {
        uint16_t expected = lastSequenceNumber_ + 1;
        if (packet.sequenceNumber != expected) {
            if (packet.sequenceNumber > expected) {
                // Packet loss detected
                stats_.packetsLost += (packet.sequenceNumber - expected);

                // Reset fragment buffer if we were in middle of fragmented NAL
                if (fragmentInProgress_) {
                    fragmentBuffer_.clear();
                    fragmentInProgress_ = false;
                }
            } else {
                // Out of order packet
                stats_.packetsOutOfOrder++;
                return false;  // Drop out-of-order packets
            }
        }
    }

    lastSequenceNumber_ = packet.sequenceNumber;
    firstPacket_ = false;
    stats_.packetsProcessed++;

    // Parse RTP header (if not already parsed)
    // For now, we assume FFmpeg already stripped RTP header and gave us payload
    const uint8_t* payload = packet.payload.data();
    size_t payloadSize = packet.payload.size();

    if (payloadSize == 0) {
        return false;
    }

    // Check NAL unit type from first byte
    uint8_t nalHeader = payload[0];
    uint8_t nalType = nalHeader & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit packet
        return processSingleNalUnit(payload, payloadSize, packet.timestamp);
    }
    else if (nalType == 28) {
        // FU-A fragmented NAL unit
        return processFragmentedNalUnit(payload, payloadSize, packet.timestamp);
    }
    else {
        std::cerr << "RtpDepacketizer: Unknown NAL type: " << static_cast<int>(nalType) << std::endl;
        return false;
    }
}

bool RtpDepacketizer::processSingleNalUnit(const uint8_t* payload, size_t size, uint32_t timestamp) {
    if (fragmentInProgress_) {
        // Unexpected single NAL while fragmenting, reset
        fragmentBuffer_.clear();
        fragmentInProgress_ = false;
    }

    NalUnit nal;
    nal.type = getNalType(payload[0]);
    nal.isKeyframe = isKeyframe(nal.type);
    nal.pts = timestamp;
    nal.dts = timestamp;

    // Copy NAL data with start code
    nal.data.resize(size + 4);
    nal.data[0] = 0x00;
    nal.data[1] = 0x00;
    nal.data[2] = 0x00;
    nal.data[3] = 0x01;
    std::memcpy(nal.data.data() + 4, payload, size);

    nalUnits_.push(std::move(nal));
    stats_.nalUnitsExtracted++;

    return true;
}

bool RtpDepacketizer::processFragmentedNalUnit(const uint8_t* payload, size_t size, uint32_t timestamp) {
    if (size < 2) {
        return false;
    }

    uint8_t fuHeader = payload[1];
    bool startBit = (fuHeader & 0x80) != 0;
    bool endBit = (fuHeader & 0x40) != 0;
    uint8_t nalType = fuHeader & 0x1F;

    if (startBit) {
        // Start of fragmented NAL unit
        if (fragmentInProgress_) {
            std::cerr << "RtpDepacketizer: New fragment started before previous completed" << std::endl;
            fragmentBuffer_.clear();
        }

        fragmentInProgress_ = true;
        fragmentTimestamp_ = timestamp;

        // Reconstruct NAL header from FU indicator and FU header
        uint8_t fuIndicator = payload[0];
        uint8_t nalHeader = (fuIndicator & 0xE0) | (fuHeader & 0x1F);

        // Add NAL start code
        fragmentBuffer_.clear();
        fragmentBuffer_.push_back(0x00);
        fragmentBuffer_.push_back(0x00);
        fragmentBuffer_.push_back(0x00);
        fragmentBuffer_.push_back(0x01);
        fragmentBuffer_.push_back(nalHeader);

        // Add fragment payload (skip FU indicator and FU header)
        fragmentBuffer_.insert(fragmentBuffer_.end(), payload + 2, payload + size);
    }
    else if (fragmentInProgress_) {
        // Middle or end fragment
        if (timestamp != fragmentTimestamp_) {
            std::cerr << "RtpDepacketizer: Fragment timestamp mismatch" << std::endl;
            fragmentBuffer_.clear();
            fragmentInProgress_ = false;
            return false;
        }

        // Add fragment payload (skip FU indicator and FU header)
        fragmentBuffer_.insert(fragmentBuffer_.end(), payload + 2, payload + size);

        if (endBit) {
            // Complete NAL unit
            completeNalUnit(timestamp);
            fragmentInProgress_ = false;
            stats_.fragmentedNalUnits++;
        }
    }
    else {
        std::cerr << "RtpDepacketizer: Fragment received without start bit" << std::endl;
        return false;
    }

    return true;
}

void RtpDepacketizer::completeNalUnit(uint32_t timestamp) {
    if (fragmentBuffer_.size() <= 4) {
        return;  // Invalid NAL unit
    }

    NalUnit nal;
    nal.type = getNalType(fragmentBuffer_[4]);  // NAL header is at index 4 (after start code)
    nal.isKeyframe = isKeyframe(nal.type);
    nal.pts = timestamp;
    nal.dts = timestamp;
    nal.data = std::move(fragmentBuffer_);

    nalUnits_.push(std::move(nal));
    stats_.nalUnitsExtracted++;

    fragmentBuffer_.clear();
}

bool RtpDepacketizer::getNalUnit(NalUnit& nalUnit) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (nalUnits_.empty()) {
        return false;
    }

    nalUnit = std::move(nalUnits_.front());
    nalUnits_.pop();
    return true;
}

bool RtpDepacketizer::hasNalUnits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !nalUnits_.empty();
}

size_t RtpDepacketizer::getNalUnitCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nalUnits_.size();
}

void RtpDepacketizer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!nalUnits_.empty()) {
        nalUnits_.pop();
    }

    fragmentBuffer_.clear();
    fragmentInProgress_ = false;
    firstPacket_ = true;
    lastSequenceNumber_ = 0;
}

RtpDepacketizer::Stats RtpDepacketizer::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

NalUnitType RtpDepacketizer::getNalType(uint8_t header) const {
    uint8_t type = header & 0x1F;
    return static_cast<NalUnitType>(type);
}

bool RtpDepacketizer::isKeyframe(NalUnitType type) const {
    return type == NalUnitType::IDR ||
           type == NalUnitType::SPS ||
           type == NalUnitType::PPS;
}

bool RtpDepacketizer::parseRtpHeader(const std::vector<uint8_t>& data,
                                     RtpHeader& header,
                                     size_t& headerSize) {
    if (data.size() < 12) {
        return false;  // Minimum RTP header size
    }

    const uint8_t* buf = data.data();

    header.version = (buf[0] >> 6) & 0x03;
    header.padding = (buf[0] & 0x20) != 0;
    header.extension = (buf[0] & 0x10) != 0;
    header.csrcCount = buf[0] & 0x0F;
    header.marker = (buf[1] & 0x80) != 0;
    header.payloadType = buf[1] & 0x7F;

    header.sequenceNumber = (buf[2] << 8) | buf[3];
    header.timestamp = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
    header.ssrc = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];

    headerSize = 12 + (header.csrcCount * 4);

    if (header.extension) {
        if (data.size() < headerSize + 4) {
            return false;
        }
        uint16_t extLength = (buf[headerSize + 2] << 8) | buf[headerSize + 3];
        headerSize += 4 + (extLength * 4);
    }

    if (data.size() < headerSize) {
        return false;
    }

    return true;
}

} // namespace network
} // namespace fluxvision
