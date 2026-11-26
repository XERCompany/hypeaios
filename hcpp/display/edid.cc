#include "hcpp/display/edid.hpp"

namespace hcpp::display {

    namespace {

        constexpr u8 cta_extension_tag = 0x02;
        constexpr u8 cta_data_block_extended = 0x07;
        constexpr u8 cta_extended_colorimetry = 0x05;
        constexpr u8 cta_extended_hdr_static_metadata = 0x06;

        void parse_data_block(const u8* data_block, u8 length, CtaExtensionInfo& result) noexcept {
            const u8 tag = data_block[0] >> 5;
            if (tag != cta_data_block_extended || length < 2) {
                return;
            }

            if (data_block[1] == cta_extended_hdr_static_metadata) {
                result.hdr.present = true;
                result.hdr.eotf_flags = data_block[2];
                result.hdr.metadata_descriptor_flags = data_block[3];
                if (length > 3) {
                    result.hdr.max_luminance = data_block[4];
                    result.hdr.has_max_luminance = true;
                }
                if (length > 4) {
                    result.hdr.max_frame_average_luminance = data_block[5];
                    result.hdr.has_max_frame_average_luminance = true;
                }
                if (length > 5) {
                    result.hdr.min_luminance = data_block[6];
                    result.hdr.has_min_luminance = true;
                }
                return;
            }

            if (data_block[1] == cta_extended_colorimetry && length >= 3) {
                result.colorimetry.present = true;
                result.colorimetry.colorimetry_flags = data_block[2];
                result.colorimetry.metadata_profiles = length >= 4 ? (data_block[3] & 0x0F) : 0;
            }
        }

    }  // namespace

    bool edid_checksum_ok(const u8* data) noexcept {
        u8 sum = 0;
        for (size_t i = 0; i < 128; ++i) {
            sum = static_cast<u8>(sum + data[i]);
        }
        return sum == 0;
    }

    bool edid_header_valid(const u8* data) noexcept {
        return data[0] == 0x00 && data[1] == 0xFF && data[2] == 0xFF && data[3] == 0xFF && data[4] == 0xFF &&
               data[5] == 0xFF && data[6] == 0xFF && data[7] == 0x00;
    }

    void copy_edid_descriptor_text(char* output, size_t output_size, const u8* descriptor) noexcept {
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

    EdidInfo parse_edid_base(const u8* data) noexcept {
        EdidInfo info;
        info.header_valid = edid_header_valid(data);
        info.checksum_ok = edid_checksum_ok(data);

        const u16 manufacturer_id = static_cast<u16>((data[8] << 8) | data[9]);
        info.manufacturer[0] = static_cast<char>('@' + ((manufacturer_id >> 10) & 0x1F));
        info.manufacturer[1] = static_cast<char>('@' + ((manufacturer_id >> 5) & 0x1F));
        info.manufacturer[2] = static_cast<char>('@' + (manufacturer_id & 0x1F));
        info.manufacturer[3] = '\0';

        info.product_code = static_cast<u16>(data[10] | (data[11] << 8));
        info.serial_number = static_cast<u32>(data[12]) | (static_cast<u32>(data[13]) << 8) |
                             (static_cast<u32>(data[14]) << 16) | (static_cast<u32>(data[15]) << 24);
        info.manufacture_week = data[16];
        info.manufacture_year = static_cast<u16>(1990 + data[17]);
        info.version = data[18];
        info.revision = data[19];
        info.digital_input = (data[20] & 0x80) != 0;
        info.width_cm = data[21];
        info.height_cm = data[22];
        info.gamma_x100 = data[23] == 0xFF ? 0 : static_cast<u16>(data[23] + 100);
        info.feature_support = data[24];
        info.dpms_standby = (data[24] & 0x80) != 0;
        info.dpms_suspend = (data[24] & 0x40) != 0;
        info.dpms_active_off = (data[24] & 0x20) != 0;
        info.standard_srgb = (data[24] & 0x04) != 0;
        info.preferred_timing = (data[24] & 0x02) != 0;
        info.continuous_timings = (data[24] & 0x01) != 0;
        info.extension_count = data[126];

        for (size_t block = 0; block < 4; ++block) {
            const u8* descriptor = data + 54 + block * 18;
            if (descriptor[0] == 0 && descriptor[1] == 0 && descriptor[2] == 0 && descriptor[3] == 0xFC) {
                copy_edid_descriptor_text(info.monitor_name, sizeof(info.monitor_name), descriptor);
                break;
            }
        }

        return info;
    }

    CtaExtensionInfo parse_cta_extension(const u8* extension) noexcept {
        CtaExtensionInfo result;
        if (extension[0] != cta_extension_tag || !edid_checksum_ok(extension)) {
            return result;
        }

        result.present = true;
        u8 end = extension[2];
        if (end == 0 || end > 127) {
            end = 127;
        }

        size_t position = 4;
        while (position < end) {
            const u8 header = extension[position];
            const u8 length = header & 0x1F;
            if (length == 0 || position + length > 127) {
                break;
            }
            parse_data_block(extension + position, length, result);
            position += static_cast<size_t>(length) + 1;
        }

        return result;
    }

    CtaExtensionInfo scan_edid_cta_extensions(const u8* edid, u8 block_count) noexcept {
        CtaExtensionInfo combined;
        for (u8 block = 1; block < block_count && block < 4; ++block) {
            const CtaExtensionInfo current = parse_cta_extension(edid + static_cast<size_t>(block) * 128);
            if (!current.present) {
                continue;
            }

            combined.present = true;
            if (current.hdr.present && !combined.hdr.present) {
                combined.hdr = current.hdr;
            }
            if (current.colorimetry.present && !combined.colorimetry.present) {
                combined.colorimetry = current.colorimetry;
            }
        }
        return combined;
    }

    HdrStaticMetadataBlock scan_edid_hdr_static_metadata(const u8* edid, u8 block_count) noexcept {
        return scan_edid_cta_extensions(edid, block_count).hdr;
    }

    bool colorimetry_bt2020_rgb_supported(u8 flags) noexcept { return (flags & 0x80) != 0; }

    bool colorimetry_bt2020_ycc_supported(u8 flags) noexcept { return (flags & 0x40) != 0; }

}  // namespace hcpp::display
