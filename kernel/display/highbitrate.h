#pragma once

#include "kernel/display/lowbitrate.h"

namespace hype::display {

    constexpr BitrateEntry high_bitrate_table[] = {
        {480, 2500},
        {720, 4500},
        {1080, 10000},
        {1440, 20000},
        {2160, 45000},
        {4320, 120000},
    };

    constexpr hcpp::u32 high_bitrate_for(const DisplayMode& mode) noexcept {
        return high_bitrate_table[bitrate_class_index(mode)].kbps;
    }

    constexpr hcpp::u32 bitrate_for(BitrateTier tier, const DisplayMode& mode) noexcept {
        return tier == BitrateTier::Low ? low_bitrate_for(mode) : high_bitrate_for(mode);
    }

}  // namespace hype::display
