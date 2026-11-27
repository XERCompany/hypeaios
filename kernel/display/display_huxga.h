#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"

namespace hype::display {

    constexpr TimingBlock huxga_timing_block{
        6400, 48, 64, 88,
        4800, 3, 4, 33,
        1916640, 60
    };

    constexpr const TimingBlock* huxga_timing() noexcept { return &huxga_timing_block; }

    constexpr const DisplayMode* huxga_mode() noexcept { return find_mode(6400, 4800); }

    constexpr hcpp::u32 huxga_pixel_clock_khz() noexcept {
        return timing_pixel_clock_khz(huxga_timing_block);
    }

}  // namespace hype::display
