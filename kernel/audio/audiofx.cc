#include "kernel/audio/audiofx.h"

extern "C" void hype_audio_apply_gain_s16(hcpp::i16 *samples, hcpp::u64 count,
                                          hcpp::u32 gain_q8) noexcept;

namespace hype::audio {

    hcpp::i16 fx_clip(hcpp::i32 value) noexcept {
        if (value > 32767) {
            return 32767;
        }
        if (value < -32768) {
            return -32768;
        }
        return static_cast<hcpp::i16>(value);
    }

    void fx_gain(hcpp::i16 *samples, hcpp::u32 frames, hcpp::u16 gain_q8) noexcept {
        if (samples == nullptr || frames == 0) {
            return;
        }
        hype_audio_apply_gain_s16(samples, frames, gain_q8);
    }

    void fx_fade(hcpp::i16 *samples, hcpp::u32 frames, hcpp::u16 from_gain_q8,
                 hcpp::u16 to_gain_q8) noexcept {
        if (samples == nullptr || frames == 0) {
            return;
        }
        const hcpp::i32 span = frames > 1 ? static_cast<hcpp::i32>(frames - 1) : 1;
        for (hcpp::u32 i = 0; i < frames; ++i) {
            const hcpp::i32 gain = static_cast<hcpp::i32>(from_gain_q8) +
                (static_cast<hcpp::i32>(to_gain_q8) - static_cast<hcpp::i32>(from_gain_q8)) *
                    static_cast<hcpp::i32>(i) / span;
            samples[i] = fx_clip((static_cast<hcpp::i32>(samples[i]) * gain) >> 8);
        }
    }

    void fx_pan(hcpp::i16 *stereo, hcpp::u32 frames, hcpp::i16 pan) noexcept {
        if (stereo == nullptr || frames == 0) {
            return;
        }
        if (pan < fx_pan_left) {
            pan = fx_pan_left;
        }
        if (pan > fx_pan_right) {
            pan = fx_pan_right;
        }
        const hcpp::u32 left_gain = static_cast<hcpp::u32>(256 - (pan > 0 ? pan : 0));
        const hcpp::u32 right_gain = static_cast<hcpp::u32>(256 + (pan < 0 ? pan : 0));
        for (hcpp::u32 i = 0; i < frames; ++i) {
            stereo[i * 2] = fx_clip((static_cast<hcpp::i32>(stereo[i * 2]) * left_gain) >> 8);
            stereo[i * 2 + 1] =
                fx_clip((static_cast<hcpp::i32>(stereo[i * 2 + 1]) * right_gain) >> 8);
        }
    }

    void fx_fade_in(hcpp::i16 *samples, hcpp::u32 frames) noexcept {
        fx_fade(samples, frames, 0, fx_gain_unity_q8);
    }

    void fx_fade_out(hcpp::i16 *samples, hcpp::u32 frames) noexcept {
        fx_fade(samples, frames, fx_gain_unity_q8, 0);
    }

}  // namespace hype::audio
