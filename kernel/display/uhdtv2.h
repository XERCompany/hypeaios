#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"
#include "kernel/display/scaleuhd.h"

namespace hype::display {

    struct Uhd2Mode {
        hcpp::u32 width;
        hcpp::u32 height;
        hcpp::u32 hz;
        hcpp::u16 vic;
    };

    constexpr hcpp::u16 vic_uhd4k_24 = 93;
    constexpr hcpp::u16 vic_uhd4k_25 = 94;
    constexpr hcpp::u16 vic_uhd4k_30 = 95;
    constexpr hcpp::u16 vic_uhd4k_50 = 96;
    constexpr hcpp::u16 vic_uhd4k_60 = 97;
    constexpr hcpp::u16 vic_uhd8k_24 = 194;
    constexpr hcpp::u16 vic_uhd8k_25 = 195;
    constexpr hcpp::u16 vic_uhd8k_30 = 196;
    constexpr hcpp::u16 vic_uhd8k_50 = 197;
    constexpr hcpp::u16 vic_uhd8k_60 = 198;

    constexpr Uhd2Mode uhd2_mode_table[] = {
        {3840, 2160, 24, vic_uhd4k_24},
        {3840, 2160, 25, vic_uhd4k_25},
        {3840, 2160, 30, vic_uhd4k_30},
        {3840, 2160, 50, vic_uhd4k_50},
        {3840, 2160, 60, vic_uhd4k_60},
        {7680, 4320, 24, vic_uhd8k_24},
        {7680, 4320, 25, vic_uhd8k_25},
        {7680, 4320, 30, vic_uhd8k_30},
        {7680, 4320, 50, vic_uhd8k_50},
        {7680, 4320, 60, vic_uhd8k_60},
    };

    constexpr hcpp::u32 uhd2_mode_count() noexcept {
        return static_cast<hcpp::u32>(sizeof(uhd2_mode_table) / sizeof(uhd2_mode_table[0]));
    }

    constexpr const Uhd2Mode* uhd2_mode_at(hcpp::u32 index) noexcept {
        return index < uhd2_mode_count() ? &uhd2_mode_table[index] : nullptr;
    }

    constexpr const Uhd2Mode* uhd_find(hcpp::u32 width, hcpp::u32 height, hcpp::u32 hz) noexcept {
        for (hcpp::u32 i = 0; i < uhd2_mode_count(); ++i) {
            if (uhd2_mode_table[i].width == width && uhd2_mode_table[i].height == height &&
                uhd2_mode_table[i].hz == hz) {
                return &uhd2_mode_table[i];
            }
        }
        return nullptr;
    }

    constexpr const Uhd2Mode* uhd_find_vic(hcpp::u16 vic) noexcept {
        for (hcpp::u32 i = 0; i < uhd2_mode_count(); ++i) {
            if (uhd2_mode_table[i].vic == vic) {
                return &uhd2_mode_table[i];
            }
        }
        return nullptr;
    }

    constexpr bool uhd2_is_uhd(hcpp::u32 width, hcpp::u32 height) noexcept {
        return (width == uhd4k_width && height == uhd4k_height) ||
               (width == uhd8k_width && height == uhd8k_height);
    }

}  // namespace hype::display
