#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"

namespace hype::display {

    constexpr TimingBlock wvga_timing_block{
        800, 40, 48, 40,
        480, 13, 3, 29,
        29232, 60
    };

    constexpr const TimingBlock* wvga_timing() noexcept { return &wvga_timing_block; }

    constexpr const DisplayMode* wvga_mode() noexcept { return find_mode(800, 480); }

    constexpr hcpp::u32 wvga_pixel_clock_khz() noexcept {
        return timing_pixel_clock_khz(wvga_timing_block);
    }

}  // namespace hype::display
