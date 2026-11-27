#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    struct DisplayMode {
        hcpp::u32 width;
        hcpp::u32 height;
        hcpp::u32 hz;
        hcpp::u32 timing_id;
    };

    struct TimingBlock {
        hcpp::u16 hactive;
        hcpp::u16 hfront;
        hcpp::u16 hsync;
        hcpp::u16 hback;
        hcpp::u16 vactive;
        hcpp::u16 vfront;
        hcpp::u16 vsync;
        hcpp::u16 vback;
        hcpp::u32 pixel_clock_khz;
        hcpp::u8 hz;
    };

    constexpr hcpp::u32 timing_generic = 0;
    constexpr hcpp::u32 timing_wvga = 1;
    constexpr hcpp::u32 timing_qxga = 2;
    constexpr hcpp::u32 timing_huxga = 3;

    constexpr TimingBlock vga_timing_block{
        640, 16, 96, 48,
        480, 10, 2, 33,
        25175, 60
    };

    constexpr DisplayMode mode_table[] = {
        {320, 200, 60, timing_generic},
        {640, 200, 60, timing_generic},
        {640, 350, 60, timing_generic},
        {640, 480, 60, timing_generic},
        {800, 480, 60, timing_wvga},
        {800, 600, 60, timing_generic},
        {1024, 768, 60, timing_generic},
        {1280, 720, 60, timing_generic},
        {1280, 800, 60, timing_generic},
        {1280, 1024, 60, timing_generic},
        {1400, 1050, 60, timing_generic},
        {1680, 1050, 60, timing_generic},
        {1600, 1200, 60, timing_generic},
        {1920, 1080, 60, timing_generic},
        {1920, 1200, 60, timing_generic},
        {2048, 1536, 60, timing_qxga},
        {2560, 1440, 60, timing_generic},
        {2560, 1600, 60, timing_generic},
        {2560, 2048, 60, timing_generic},
        {3840, 2160, 60, timing_generic},
        {3840, 2400, 60, timing_generic},
        {4096, 3072, 60, timing_generic},
        {6400, 4800, 60, timing_huxga},
        {7680, 4320, 60, timing_generic},
    };

    constexpr hcpp::u32 mode_count() noexcept {
        return static_cast<hcpp::u32>(sizeof(mode_table) / sizeof(mode_table[0]));
    }

    constexpr const DisplayMode* mode_at(hcpp::u32 index) noexcept {
        return index < mode_count() ? &mode_table[index] : nullptr;
    }

    constexpr const DisplayMode* find_mode(hcpp::u32 width, hcpp::u32 height) noexcept {
        for (hcpp::u32 i = 0; i < mode_count(); ++i) {
            if (mode_table[i].width == width && mode_table[i].height == height) {
                return &mode_table[i];
            }
        }
        return nullptr;
    }

    constexpr hcpp::u32 timing_htotal(const TimingBlock& timing) noexcept {
        return static_cast<hcpp::u32>(timing.hactive) + timing.hfront + timing.hsync +
               timing.hback;
    }

    constexpr hcpp::u32 timing_vtotal(const TimingBlock& timing) noexcept {
        return static_cast<hcpp::u32>(timing.vactive) + timing.vfront + timing.vsync +
               timing.vback;
    }

    constexpr hcpp::u32 timing_pixel_clock_khz(const TimingBlock& timing) noexcept {
        return timing_htotal(timing) * timing_vtotal(timing) * timing.hz / 1000;
    }

}  // namespace hype::display
