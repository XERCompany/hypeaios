#pragma once

#include "hcpp/types.hpp"

namespace hype::audio::opl {

    constexpr hcpp::u16 adlib_base = 0x388;
    constexpr hcpp::u16 sb_base = 0x220;
    constexpr hcpp::u8 channel_count = 9;

    bool opl_detect(hcpp::u16 base) noexcept;
    void opl_reset(hcpp::u16 base) noexcept;
    void opl_write(hcpp::u16 base, hcpp::u8 reg, hcpp::u8 val) noexcept;
    void opl_note(hcpp::u16 base, hcpp::u8 channel, hcpp::u32 freq_hz) noexcept;
    void opl_note_off(hcpp::u16 base, hcpp::u8 channel) noexcept;

}  // namespace hype::audio::opl
