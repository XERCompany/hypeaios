#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    constexpr hcpp::u16 fx_gain_unity_q8 = 256;
    constexpr hcpp::i16 fx_pan_center = 0;
    constexpr hcpp::i16 fx_pan_left = -256;
    constexpr hcpp::i16 fx_pan_right = 256;

    hcpp::i16 fx_clip(hcpp::i32 value) noexcept;
    void fx_gain(hcpp::i16 *samples, hcpp::u32 frames, hcpp::u16 gain_q8) noexcept;
    void fx_fade(hcpp::i16 *samples, hcpp::u32 frames, hcpp::u16 from_gain_q8,
                 hcpp::u16 to_gain_q8) noexcept;
    void fx_pan(hcpp::i16 *stereo, hcpp::u32 frames, hcpp::i16 pan) noexcept;
    void fx_fade_in(hcpp::i16 *samples, hcpp::u32 frames) noexcept;
    void fx_fade_out(hcpp::i16 *samples, hcpp::u32 frames) noexcept;

}  // namespace hype::audio
