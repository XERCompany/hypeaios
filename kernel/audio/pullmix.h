#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    using PullmixFill = void (*)(hcpp::i16 *dst, hcpp::u32 frames, void *ctx) noexcept;

    constexpr hcpp::u32 pullmix_max_sources = 8;
    constexpr hcpp::u32 pullmix_ring_frames = 1024;
    constexpr hcpp::u32 pullmix_max_channels = 2;
    constexpr hcpp::u32 pullmix_invalid_id = 0xFFFFFFFFu;

    struct PullmixSourceInfo {
        hcpp::u32 id;
        hcpp::u32 rate;
        hcpp::u8 channels;
        bool active;
        hcpp::u32 underruns;
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
    hcpp::u32 pullmix_render(hcpp::i16 *out, hcpp::u32 frames, hcpp::u8 channels) noexcept;
    hcpp::u32 pullmix_clip_events() noexcept;

}  // namespace hype::audio
