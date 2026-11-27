#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"

namespace hype::display {

    constexpr TimingBlock qxga_timing_block{
        2048, 48, 32, 80,
        1536, 3, 4, 38,
        209450, 60
    };

    constexpr const TimingBlock* qxga_timing() noexcept { return &qxga_timing_block; }

    constexpr const DisplayMode* qxga_mode() noexcept { return find_mode(2048, 1536); }

    constexpr hcpp::u32 qxga_pixel_clock_khz() noexcept {
        return timing_pixel_clock_khz(qxga_timing_block);
    }

}  // namespace hype::display
