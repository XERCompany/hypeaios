#pragma once

#include "hcpp/types.hpp"

namespace hype::audio::sb16 {

    constexpr hcpp::u16 default_base = 0x220;
    constexpr hcpp::u8 default_dma8 = 1;

    bool dsp_reset(hcpp::u16 base) noexcept;
    bool dsp_version(hcpp::u16 base, hcpp::u8 &major, hcpp::u8 &minor) noexcept;
    bool dsp_speaker_on(hcpp::u16 base) noexcept;
    bool dsp_set_rate(hcpp::u16 base, hcpp::u16 rate) noexcept;
    bool play_pcm8(hcpp::u16 base, hcpp::u8 dma_channel, hcpp::u16 rate,
                   const hcpp::u8 *samples, hcpp::u16 count) noexcept;

}  // namespace hype::audio::sb16
