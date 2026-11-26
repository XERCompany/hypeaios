#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    struct WaveformTone {
        hcpp::u32 freq_hz;
        hcpp::u32 rate;
        hcpp::u16 phase;
        hcpp::u16 phase_step;
    };

    inline hcpp::i16 waveform_sine_q15(hcpp::u16 phase) noexcept {
        const hcpp::i32 n = static_cast<hcpp::i32>(phase) - 32768;
        const hcpp::i32 a = n < 0 ? -n : n;
        const hcpp::i32 y = (n * (32768 - a)) >> 13;
        if (y > 32767) {
            return 32767;
        }
        if (y < -32768) {
            return -32768;
        }
        return static_cast<hcpp::i16>(y);
    }

    inline WaveformTone waveform_tone(hcpp::u32 freq_hz, hcpp::u32 rate) noexcept {
        WaveformTone tone{freq_hz, rate, 0, 0};
        if (rate != 0) {
            tone.phase_step = static_cast<hcpp::u16>((freq_hz * 65536u + rate / 2) / rate);
        }
        return tone;
    }

    inline hcpp::i16 waveform_next(WaveformTone &tone) noexcept {
        const hcpp::i16 sample = waveform_sine_q15(tone.phase);
        tone.phase = static_cast<hcpp::u16>(tone.phase + tone.phase_step);
        return sample;
    }

    hcpp::u32 waveform_render_sine(hcpp::i16 *dst, hcpp::u32 frames, hcpp::u32 freq_hz, hcpp::u32 rate,
                                   hcpp::u8 channels) noexcept;

}  // namespace hype::audio
