#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"

namespace hype::display {

    enum class BitrateTier : hcpp::u8 { Low, High };

    struct BitrateEntry {
        hcpp::u32 height;
        hcpp::u32 kbps;
    };

    constexpr BitrateEntry low_bitrate_table[] = {
        {480, 800},
        {720, 1500},
        {1080, 3000},
        {1440, 6000},
        {2160, 12000},
        {4320, 40000},
    };

    constexpr hcpp::u32 bitrate_class_count =
        static_cast<hcpp::u32>(sizeof(low_bitrate_table) / sizeof(low_bitrate_table[0]));

    constexpr hcpp::u32 bitrate_class_index(const DisplayMode& mode) noexcept {
        for (hcpp::u32 i = 0; i < bitrate_class_count; ++i) {
            if (mode.height <= low_bitrate_table[i].height) {
                return i;
            }
        }
        return bitrate_class_count - 1;
    }

    constexpr hcpp::u32 low_bitrate_for(const DisplayMode& mode) noexcept {
        return low_bitrate_table[bitrate_class_index(mode)].kbps;
    }

}  // namespace hype::display
