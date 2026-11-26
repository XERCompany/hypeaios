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
    bool digital_input = false;
    u8 width_cm = 0;
    u8 height_cm = 0;
    u16 gamma_x100 = 0;
    bool dpms_standby = false;
    bool dpms_suspend = false;
    bool dpms_active_off = false;
    bool standard_srgb = false;
    bool preferred_timing = false;
    bool continuous_timings = false;
    u8 feature_support = 0;
    char monitor_name[14]{};
    u8 extension_count = 0;
};

struct ColorimetryBlock {
    bool present = false;
    u8 colorimetry_flags = 0;
    u8 metadata_profiles = 0;
};

struct CtaExtensionInfo {
    bool present = false;
    HdrStaticMetadataBlock hdr;
    ColorimetryBlock colorimetry;
};

bool edid_checksum_ok(const u8* data) noexcept;
bool edid_header_valid(const u8* data) noexcept;
void copy_edid_descriptor_text(char* output, size_t output_size, const u8* descriptor) noexcept;
EdidInfo parse_edid_base(const u8* data) noexcept;
CtaExtensionInfo parse_cta_extension(const u8* extension) noexcept;
CtaExtensionInfo scan_edid_cta_extensions(const u8* edid, u8 block_count) noexcept;
HdrStaticMetadataBlock scan_edid_hdr_static_metadata(const u8* edid, u8 block_count) noexcept;
bool colorimetry_bt2020_rgb_supported(u8 flags) noexcept;
bool colorimetry_bt2020_ycc_supported(u8 flags) noexcept;

} // namespace hcpp::display
