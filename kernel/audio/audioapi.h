#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    enum class AudioSampleFormat : hcpp::u8 { U8, S16Le, Float32 };

    struct AudioFormat {
        hcpp::u32 sample_rate;
        hcpp::u8 channels;
        AudioSampleFormat sample_format;
    };

    using AudioStreamId = hcpp::u32;
    constexpr AudioStreamId audio_stream_invalid = 0;
    constexpr hcpp::u32 audio_default_rate = 48000;

    inline hcpp::u32 audio_sample_bytes(AudioSampleFormat format) noexcept {
        switch (format) {
            case AudioSampleFormat::S16Le:
                return 2;
            case AudioSampleFormat::Float32:
                return 4;
            default:
                return 1;
        }
    }

    inline hcpp::u32 audio_frame_bytes(const AudioFormat &format) noexcept {
        return audio_sample_bytes(format.sample_format) * format.channels;
    }

    bool audio_stream_open(const AudioFormat &format, AudioStreamId &stream) noexcept;
    bool audio_stream_write(AudioStreamId stream, const void *frames, hcpp::u32 frame_count) noexcept;
    bool audio_stream_set_volume(AudioStreamId stream, hcpp::u8 percent) noexcept;
    void audio_stream_close(AudioStreamId stream) noexcept;

}  // namespace hype::audio
