#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    struct AudioPlaybackRequest {
        AudioFormat format;
        hcpp::u32 tone_hz;
        hcpp::u32 duration_ms;
        hcpp::u8 volume;
    };

    bool audio_play_tone(const AudioPlaybackRequest &request) noexcept;
    bool audio_play_buffer(const AudioFormat &format, const void *frames, hcpp::u32 frame_count) noexcept;
    hcpp::u32 audio_master_volume() noexcept;
    void audio_set_master_volume(hcpp::u8 percent) noexcept;

}  // namespace hype::audio
