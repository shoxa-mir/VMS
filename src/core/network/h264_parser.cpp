#include "h264_parser.h"
#include <cstring>
#include <iostream>

namespace fluxvision {
namespace network {

// BitReader implementation
H264Parser::BitReader::BitReader(const uint8_t* data, size_t size)
    : data_(data), size_(size) {}

uint32_t H264Parser::BitReader::readBits(int numBits) {
    if (numBits == 0 || !hasMoreData()) {
        return 0;
    }

    uint32_t result = 0;
    for (int i = 0; i < numBits; i++) {
        if (bytePos_ >= size_) {
            return result;
        }

        int bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;
        result = (result << 1) | bit;

        bitPos_++;
        if (bitPos_ >= 8) {
            bitPos_ = 0;
            bytePos_++;
        }
    }

    return result;
}

uint32_t H264Parser::BitReader::readUE() {
    // Read unsigned exponential-Golomb code
    int leadingZeros = 0;
    while (hasMoreData() && readBits(1) == 0) {
        leadingZeros++;
        if (leadingZeros > 32) {
            return 0;  // Invalid code
        }
    }

    if (leadingZeros == 0) {
        return 0;
    }

    uint32_t value = readBits(leadingZeros);
    return (1 << leadingZeros) - 1 + value;
}

int32_t H264Parser::BitReader::readSE() {
    // Read signed exponential-Golomb code
    uint32_t code = readUE();
    if (code % 2 == 0) {
        return -static_cast<int32_t>(code / 2);
    } else {
        return static_cast<int32_t>((code + 1) / 2);
    }
}

bool H264Parser::BitReader::hasMoreData() const {
    return bytePos_ < size_ || (bytePos_ == size_ - 1 && bitPos_ < 8);
}

// H264Parser implementation
H264Parser::NalInfo H264Parser::parseNalHeader(const uint8_t* data, size_t size) {
    NalInfo info;

    if (!data || size == 0) {
        return info;
    }

    // Skip start code if present
    size_t nalSize = 0;
    const uint8_t* nalData = skipStartCode(data, size, nalSize);
    if (!nalData) {
        nalData = data;
        nalSize = size;
    }

    if (nalSize == 0) {
        return info;
    }

    // Parse NAL header (first byte)
    uint8_t nalHeader = nalData[0];
    info.refIdc = (nalHeader >> 5) & 0x03;
    info.type = static_cast<NalUnitType>(nalHeader & 0x1F);
    info.isKeyframe = (info.type == NalUnitType::IDR ||
                      info.type == NalUnitType::SPS ||
                      info.type == NalUnitType::PPS);

    return info;
}

bool H264Parser::extractSPS(const uint8_t* data, size_t size, SPSInfo& sps) {
    if (!data || size < 4) {
        return false;
    }

    // Skip start code
    size_t nalSize = 0;
    const uint8_t* nalData = skipStartCode(data, size, nalSize);
    if (!nalData) {
        nalData = data;
        nalSize = size;
    }

    // Check NAL type
    if ((nalData[0] & 0x1F) != static_cast<uint8_t>(NalUnitType::SPS)) {
        return false;
    }

    // Skip NAL header and parse SPS
    BitReader reader(nalData + 1, nalSize - 1);

    try {
        return parseSPS_Simple(reader, sps);
    } catch (...) {
        std::cerr << "H264Parser: Exception while parsing SPS" << std::endl;
        return false;
    }
}

bool H264Parser::parseSPS_Simple(BitReader& reader, SPSInfo& sps) {
    // Parse profile and level
    sps.profile = reader.readBits(8);
    reader.readBits(8);  // constraint_set_flags
    sps.level = reader.readBits(8);

    // sps_id
    reader.readUE();

    // For high profiles, parse chroma format
    if (sps.profile == 100 || sps.profile == 110 || sps.profile == 122 ||
        sps.profile == 244 || sps.profile == 44 || sps.profile == 83 ||
        sps.profile == 86 || sps.profile == 118 || sps.profile == 128) {

        uint32_t chroma_format_idc = reader.readUE();
        if (chroma_format_idc == 3) {
            reader.readBits(1);  // separate_colour_plane_flag
        }

        reader.readUE();  // bit_depth_luma_minus8
        reader.readUE();  // bit_depth_chroma_minus8
        reader.readBits(1);  // qpprime_y_zero_transform_bypass_flag

        if (reader.readBits(1)) {  // seq_scaling_matrix_present_flag
            // Skip scaling matrices (simplified)
            for (int i = 0; i < 8; i++) {
                if (reader.readBits(1)) {
                    int size = (i < 6) ? 16 : 64;
                    for (int j = 0; j < size; j++) {
                        reader.readSE();
                    }
                }
            }
        }
    }

    // log2_max_frame_num_minus4
    reader.readUE();

    // pic_order_cnt_type
    uint32_t pic_order_cnt_type = reader.readUE();
    if (pic_order_cnt_type == 0) {
        reader.readUE();  // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
        reader.readBits(1);  // delta_pic_order_always_zero_flag
        reader.readSE();     // offset_for_non_ref_pic
        reader.readSE();     // offset_for_top_to_bottom_field

        uint32_t num_ref_frames_in_pic_order_cnt_cycle = reader.readUE();
        for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            reader.readSE();  // offset_for_ref_frame
        }
    }

    // num_ref_frames
    reader.readUE();

    // gaps_in_frame_num_value_allowed_flag
    reader.readBits(1);

    // Resolution
    uint32_t pic_width_in_mbs_minus1 = reader.readUE();
    uint32_t pic_height_in_map_units_minus1 = reader.readUE();

    sps.width = (pic_width_in_mbs_minus1 + 1) * 16;
    sps.height = (pic_height_in_map_units_minus1 + 1) * 16;

    // frame_mbs_only_flag
    uint32_t frame_mbs_only_flag = reader.readBits(1);
    sps.interlaced = (frame_mbs_only_flag == 0);

    if (!frame_mbs_only_flag) {
        sps.height *= 2;
        reader.readBits(1);  // mb_adaptive_frame_field_flag
    }

    // direct_8x8_inference_flag
    reader.readBits(1);

    // Frame cropping
    if (reader.readBits(1)) {  // frame_cropping_flag
        uint32_t left = reader.readUE();
        uint32_t right = reader.readUE();
        uint32_t top = reader.readUE();
        uint32_t bottom = reader.readUE();

        // Apply cropping (simplified, assumes 4:2:0)
        sps.width -= (left + right) * 2;
        sps.height -= (top + bottom) * 2;
    }

    // VUI parameters (for framerate)
    if (reader.readBits(1)) {  // vui_parameters_present_flag
        if (reader.readBits(1)) {  // aspect_ratio_info_present_flag
            uint32_t aspect_ratio_idc = reader.readBits(8);
            if (aspect_ratio_idc == 255) {  // Extended_SAR
                reader.readBits(16);  // sar_width
                reader.readBits(16);  // sar_height
            }
        }

        if (reader.readBits(1)) {  // overscan_info_present_flag
            reader.readBits(1);
        }

        if (reader.readBits(1)) {  // video_signal_type_present_flag
            reader.readBits(3);  // video_format
            reader.readBits(1);  // video_full_range_flag
            if (reader.readBits(1)) {  // colour_description_present_flag
                reader.readBits(8);  // colour_primaries
                reader.readBits(8);  // transfer_characteristics
                reader.readBits(8);  // matrix_coefficients
            }
        }

        if (reader.readBits(1)) {  // chroma_loc_info_present_flag
            reader.readUE();  // chroma_sample_loc_type_top_field
            reader.readUE();  // chroma_sample_loc_type_bottom_field
        }

        if (reader.readBits(1)) {  // timing_info_present_flag
            uint32_t num_units_in_tick = reader.readBits(32);
            uint32_t time_scale = reader.readBits(32);

            if (num_units_in_tick > 0) {
                sps.framerate = time_scale / (2 * num_units_in_tick);
            }
        }
    }

    // Default framerate if not found
    if (sps.framerate == 0) {
        sps.framerate = 25;  // Common default
    }

    return true;
}

bool H264Parser::extractPPS(const uint8_t* data, size_t size, PPSInfo& pps) {
    if (!data || size < 2) {
        return false;
    }

    // Skip start code
    size_t nalSize = 0;
    const uint8_t* nalData = skipStartCode(data, size, nalSize);
    if (!nalData) {
        nalData = data;
        nalSize = size;
    }

    // Check NAL type
    if ((nalData[0] & 0x1F) != static_cast<uint8_t>(NalUnitType::PPS)) {
        return false;
    }

    // Skip NAL header and parse PPS
    BitReader reader(nalData + 1, nalSize - 1);

    pps.ppsId = reader.readUE();
    pps.spsId = reader.readUE();
    pps.entropyCodingMode = reader.readBits(1);  // entropy_coding_mode_flag

    return true;
}

bool H264Parser::isKeyframe(const uint8_t* data, size_t size) {
    NalInfo info = parseNalHeader(data, size);
    return info.isKeyframe;
}

NalUnitType H264Parser::getNalType(const uint8_t* data, size_t size) {
    NalInfo info = parseNalHeader(data, size);
    return info.type;
}

bool H264Parser::hasStartCode(const uint8_t* data, size_t size) {
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00) {
        return (data[2] == 0x00 && data[3] == 0x01) ||  // 4-byte start code
               (data[2] == 0x01);                        // 3-byte start code
    }
    return false;
}

const uint8_t* H264Parser::skipStartCode(const uint8_t* data, size_t size, size_t& nalSize) {
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
        data[2] == 0x00 && data[3] == 0x01) {
        // 4-byte start code
        nalSize = size - 4;
        return data + 4;
    }
    else if (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        // 3-byte start code
        nalSize = size - 3;
        return data + 3;
    }

    nalSize = 0;
    return nullptr;
}

} // namespace network
} // namespace fluxvision
