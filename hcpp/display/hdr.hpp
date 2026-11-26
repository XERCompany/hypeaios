#pragma once

#include "hcpp/types.hpp"

namespace hcpp::display {

struct HdrMetadata {
    float max_luminance = 0.0f;
    float min_luminance = 0.0f;
    float max_frame_average = 0.0f;
    bool supports_hdr = false;
    bool supports_dolby_vision = false;
};

struct HdrStaticMetadataBlock {
    bool present = false;
    u8 eotf_flags = 0;
    u8 metadata_descriptor_flags = 0;
    u8 max_luminance = 0;
    u8 max_frame_average_luminance = 0;
    u8 min_luminance = 0;
    bool has_max_luminance = false;
    bool has_max_frame_average_luminance = false;
    bool has_min_luminance = false;
};

bool hdr_eotf_pq_supported(u8 eotf_flags) noexcept;
bool hdr_eotf_hlg_supported(u8 eotf_flags) noexcept;

} // namespace hcpp::display
