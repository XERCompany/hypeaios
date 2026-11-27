#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"

namespace hype::display {

    constexpr hcpp::u32 timing_pal = 4;

    constexpr hcpp::u32 pal_total_lines = 625;
    constexpr hcpp::u32 pal_active_lines = 576;
    constexpr hcpp::u32 pal_field_hz = 50;
    constexpr hcpp::u32 pal_frame_hz = 25;
    constexpr hcpp::u32 pal_line_rate_hz = 15625;
    constexpr hcpp::u32 pal_line_period_us = 64;
    constexpr hcpp::u32 pal_blank_us = 12;
    constexpr hcpp::u32 pal_hsync_us_x10 = 47;
    constexpr hcpp::u32 pal_active_samples = 720;
    constexpr hcpp::u32 pal_total_samples = 864;
    constexpr hcpp::u32 pal_clock_khz = 13500;

    constexpr TimingBlock pal_timing_block{
        720, 16, 64, 64,
        576, 5, 5, 39,
        13500, 50
    };

    constexpr const TimingBlock* pal_timing() noexcept { return &pal_timing_block; }

    constexpr hcpp::u32 pal_pixel_clock_khz() noexcept {
        return pal_timing_block.pixel_clock_khz;
    }

    constexpr hcpp::u32 pal_line_rate() noexcept {
        return pal_timing_block.pixel_clock_khz * 1000 / timing_htotal(pal_timing_block);
    }

    constexpr bool pal_sane() noexcept {
        return pal_line_rate() == pal_line_rate_hz &&
               timing_htotal(pal_timing_block) == pal_total_samples &&
               timing_vtotal(pal_timing_block) == pal_total_lines &&
               pal_timing_block.vactive == pal_active_lines &&
               pal_timing_block.hz == pal_field_hz;
    }

}  // namespace hype::display
