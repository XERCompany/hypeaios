#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    using PullmixFill = void (*)(hcpp::i16 *dst, hcpp::u32 frames, void *ctx) noexcept;

    constexpr hcpp::u32 pullmix_max_sources = 8;
    constexpr hcpp::u32 pullmix_ring_frames = 1024;
    constexpr hcpp::u32 pullmix_max_channels = 2;
    constexpr hcpp::u32 pullmix_invalid_id = 0xFFFFFFFFu;
    constexpr hcpp::u32 pullmix_default_rate = 48000;
    constexpr hcpp::u16 pullmix_gain_unity_q8 = 256;
    constexpr hcpp::i16 pullmix_pan_left = -256;
    constexpr hcpp::i16 pullmix_pan_right = 256;

    struct PullmixSourceInfo {
        hcpp::u32 id;
        hcpp::u32 rate;
        hcpp::u8 channels;
        bool active;
        bool muted;
        bool solo;
        hcpp::i16 pan;
        hcpp::u16 gain_q8;
        hcpp::u32 underruns;
        hcpp::u32 overflows;
    };

    inline hcpp::i16 pullmix_clip16(hcpp::i32 sample) noexcept {
        if (sample > 32767) {
            return 32767;
        }
        if (sample < -32768) {
            return -32768;
        }
        return static_cast<hcpp::i16>(sample);
    }

    void pullmix_reset() noexcept;
    hcpp::u32 pullmix_add_source(PullmixFill fill, void *ctx, hcpp::u8 channels,
                               hcpp::u32 rate) noexcept;
    bool pullmix_remove_source(hcpp::u32 id) noexcept;
    hcpp::u32 pullmix_source_count() noexcept;
    bool pullmix_source_info(hcpp::u32 index, PullmixSourceInfo &info) noexcept;
    void pullmix_set_output_rate(hcpp::u32 rate) noexcept;
    hcpp::u32 pullmix_output_rate() noexcept;
    bool pullmix_set_source_gain(hcpp::u32 id, hcpp::u16 gain_q8) noexcept;
    bool pullmix_set_source_pan(hcpp::u32 id, hcpp::i16 pan) noexcept;
    bool pullmix_set_source_muted(hcpp::u32 id, bool muted) noexcept;
    bool pullmix_set_source_solo(hcpp::u32 id, bool solo) noexcept;
    void pullmix_set_master_gain(hcpp::u16 gain_q8) noexcept;
    hcpp::u16 pullmix_master_gain() noexcept;
    hcpp::u32 pullmix_render(hcpp::i16 *out, hcpp::u32 frames, hcpp::u8 channels) noexcept;
    hcpp::u32 pullmix_clip_events() noexcept;
    bool pullmix_selftest() noexcept;

}  // namespace hype::audio
