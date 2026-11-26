#pragma once

#include "hcpp/display/hdr.hpp"
#include "hcpp/types.hpp"

namespace hcpp::display {

struct EdidInfo {
    bool header_valid = false;
    bool checksum_ok = false;
    char manufacturer[4]{};
    u16 product_code = 0;
    u32 serial_number = 0;
    u8 manufacture_week = 0;
    u16 manufacture_year = 0;
    u8 version = 0;
    u8 revision = 0;
    u8 width_cm = 0;
    u8 height_cm = 0;
    u16 gamma_x100 = 0;
    u8 feature_support = 0;
    char monitor_name[14]{};
    u8 extension_count = 0;
};

inline bool edid_checksum_ok(const u8* data) noexcept {
    u8 sum = 0;
    for (size_t i = 0; i < 128; ++i) {
        sum = static_cast<u8>(sum + data[i]);
    }
    return sum == 0;
}

inline bool edid_header_valid(const u8* data) noexcept {
    return data[0] == 0x00 && data[1] == 0xFF && data[2] == 0xFF &&
           data[3] == 0xFF && data[4] == 0xFF && data[5] == 0xFF &&
           data[6] == 0xFF && data[7] == 0x00;
}

inline void copy_edid_descriptor_text(char* output, size_t output_size, const u8* descriptor) noexcept {
    size_t length = 0;
    for (size_t i = 5; i < 18 && length + 1 < output_size; ++i) {
        const char value = static_cast<char>(descriptor[i]);
        if (value == '\n' || value == '\r' || value == '\0') {
            break;
        }
        output[length++] = value;
    }
    while (length > 0 && output[length - 1] == ' ') {
        --length;
    }
    output[length] = '\0';
}

inline EdidInfo parse_edid_base(const u8* data) noexcept {
    EdidInfo info;
    info.header_valid = edid_header_valid(data);
    info.checksum_ok = edid_checksum_ok(data);

    const u16 manufacturer_id = static_cast<u16>((data[8] << 8) | data[9]);
    info.manufacturer[0] = static_cast<char>('@' + ((manufacturer_id >> 10) & 0x1F));
    info.manufacturer[1] = static_cast<char>('@' + ((manufacturer_id >> 5) & 0x1F));
    info.manufacturer[2] = static_cast<char>('@' + (manufacturer_id & 0x1F));
    info.manufacturer[3] = '\0';

    info.product_code = static_cast<u16>(data[10] | (data[11] << 8));
    info.serial_number = static_cast<u32>(data[12]) |
                         (static_cast<u32>(data[13]) << 8) |
                         (static_cast<u32>(data[14]) << 16) |
                         (static_cast<u32>(data[15]) << 24);
    info.manufacture_week = data[16];
    info.manufacture_year = static_cast<u16>(1990 + data[17]);
    info.version = data[18];
    info.revision = data[19];
    info.width_cm = data[21];
    info.height_cm = data[22];
    info.gamma_x100 = data[23] == 0xFF ? 0 : static_cast<u16>(data[23] + 100);
    info.feature_support = data[24];
    info.extension_count = data[126];

    for (size_t block = 0; block < 4; ++block) {
        const u8* descriptor = data + 54 + block * 18;
        if (descriptor[0] == 0 && descriptor[1] == 0 &&
            descriptor[2] == 0 && descriptor[3] == 0xFC) {
            copy_edid_descriptor_text(info.monitor_name, sizeof(info.monitor_name), descriptor);
            break;
        }
    }

    return info;
}

inline HdrStaticMetadataBlock parse_cta_hdr_static_metadata(const u8* extension) noexcept {
    HdrStaticMetadataBlock result;
    if (extension[0] != 0x02) {
        return result;
    }

    u8 end = extension[2];
    if (end == 0 || end > 127) {
        end = 127;
    }

    size_t position = 4;
    while (position < end) {
        const u8 header = extension[position];
        const u8 tag = header >> 5;
        const u8 length = header & 0x1F;
        if (length == 0 || position + length >= 128) {
            break;
        }

        const u8* data_block = extension + position;
        if (tag == 0x07 && length >= 2 && data_block[1] == 0x06) {
            result.present = true;
            result.eotf_flags = data_block[2];
            result.metadata_descriptor_flags = data_block[3];
            if (length > 3) {
                result.max_luminance = data_block[4];
                result.has_max_luminance = true;
            }
            if (length > 4) {
                result.max_frame_average_luminance = data_block[5];
                result.has_max_frame_average_luminance = true;
            }
            if (length > 5) {
                result.min_luminance = data_block[6];
                result.has_min_luminance = true;
            }
            return result;
        }

        position += static_cast<size_t>(length) + 1;
    }

    return result;
}

inline HdrStaticMetadataBlock scan_edid_hdr_static_metadata(const u8* edid, u8 block_count) noexcept {
    HdrStaticMetadataBlock result;
    for (u8 block = 1; block < block_count && block < 4; ++block) {
        result = parse_cta_hdr_static_metadata(edid + static_cast<size_t>(block) * 128);
        if (result.present) {
            return result;
        }
    }
    return result;
}

} // namespace hcpp::display
