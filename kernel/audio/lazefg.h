#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    hcpp::i16 lazefg_clip(hcpp::i32 value) noexcept;
    void lazefg_apply_ramp(hcpp::i16 *samples, hcpp::u32 count, hcpp::u16 start_gain_q8,
                           hcpp::u16 end_gain_q8) noexcept;
    void lazefg_fade_in(hcpp::i16 *samples, hcpp::u32 count) noexcept;
    void lazefg_fade_out(hcpp::i16 *samples, hcpp::u32 count) noexcept;

}  // namespace hype::audio
