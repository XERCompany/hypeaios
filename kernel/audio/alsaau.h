#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    struct AlsaPcmConfig {
        AudioFormat format;
        hcpp::u32 period_frames;
        hcpp::u32 period_count;
    };

    constexpr int alsa_error = -1;

    using SndPcmHandle = int;
    constexpr SndPcmHandle snd_pcm_invalid = -1;

    int snd_pcm_open(const AlsaPcmConfig &config) noexcept;
    int snd_pcm_writei(SndPcmHandle handle, const void *frames, hcpp::u32 frame_count) noexcept;
    int snd_pcm_prepare(SndPcmHandle handle) noexcept;
    int snd_pcm_drain(SndPcmHandle handle) noexcept;
    int snd_pcm_close(SndPcmHandle handle) noexcept;
    int snd_card_count() noexcept;

    int alsa_pcm_open(const AlsaPcmConfig &config) noexcept;
    int alsa_pcm_writei(int handle, const void *frames, hcpp::u32 frame_count) noexcept;
    int alsa_pcm_prepare(int handle) noexcept;
    void alsa_pcm_close(int handle) noexcept;

}  // namespace hype::audio
