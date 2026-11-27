#pragma once
#include "hcpp/types.hpp"
namespace hype::video {
    constexpr hcpp::u32 v4lv_key_make(char a, char b, char c, char d) noexcept {
        return hcpp::u8(a) | hcpp::u32(hcpp::u8(b)) << 8 | hcpp::u32(hcpp::u8(c)) << 16 | hcpp::u32(hcpp::u8(d)) << 24;
    }
    constexpr hcpp::u32 v4lv_key_yuyv = v4lv_key_make('Y', 'U', 'Y', 'V');
    constexpr hcpp::u32 v4lv_key_nv12 = v4lv_key_make('N', 'V', '1', '2');
    constexpr hcpp::u32 v4lv_key_i420 = v4lv_key_make('I', '4', '2', '0');
    constexpr hcpp::u32 v4lv_key_mjpg = v4lv_key_make('M', 'J', 'P', 'G');
    constexpr hcpp::u32 v4lv_key_rgb3 = v4lv_key_make('R', 'G', 'B', '3');
    constexpr hcpp::u32 v4lv_key_rgb4 = v4lv_key_make('R', 'G', 'B', '4');
    bool v4lv_key_valid(hcpp::u32) noexcept;
    void v4lv_key_name(hcpp::u32, char*) noexcept;
}  // namespace hype::video
