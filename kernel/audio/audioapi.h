#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    enum class AudioSampleFormat : hcpp::u8 { U8, S16Le, Float32 };
    enum class AudioStreamState : hcpp::u8 { Idle, Running, Paused };

    using AudioStreamId = hcpp::u32;
    constexpr AudioStreamId audio_stream_invalid = 0;
    constexpr hcpp::u32 audio_default_rate = 48000;
    constexpr hcpp::u16 audio_gain_unity_q8 = 256;

    struct AudioFormat {
        hcpp::u32 sample_rate;
        hcpp::u8 channels;
        AudioSampleFormat sample_format;
    };

    struct AudioStreamInfo {
        AudioStreamId id;
        AudioFormat format;
        hcpp::u16 gain_q8;
        AudioStreamState state;
        hcpp::u32 frames_queued;
        hcpp::u32 underruns;
    };

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
    bool audio_stream_set_gain(AudioStreamId stream, hcpp::u16 gain_q8) noexcept;
    hcpp::u16 audio_stream_gain(AudioStreamId stream) noexcept;
    bool audio_stream_set_state(AudioStreamId stream, AudioStreamState state) noexcept;
    bool audio_stream_info(AudioStreamId stream, AudioStreamInfo &info) noexcept;
    bool audio_stream_info_at(hcpp::u32 slot, AudioStreamInfo &info) noexcept;
    hcpp::u32 audio_stream_count() noexcept;
    void audio_stream_close(AudioStreamId stream) noexcept;

    struct MixClipStats {
        hcpp::u32 clipped;
        hcpp::u32 peak;
    };

    void mix_clear(hcpp::i16 *dst, hcpp::u32 frames) noexcept;
    void mix_add(hcpp::i16 *dst, const hcpp::i16 *src, hcpp::u32 frames,
                 hcpp::u16 gain_q8) noexcept;
    MixClipStats mix_clip_stats(const hcpp::i16 *samples, hcpp::u32 frames) noexcept;
    bool mix_selftest() noexcept;

    hcpp::u32 src_output_frames(hcpp::u32 in_frames, hcpp::u32 in_rate,
                                hcpp::u32 out_rate) noexcept;
    hcpp::u32 src_convert(const hcpp::i16 *in, hcpp::u32 in_frames, hcpp::u32 in_rate,
                          hcpp::i16 *out, hcpp::u32 out_capacity, hcpp::u32 out_rate) noexcept;
    bool src_selftest() noexcept;

    void pcm_mono_to_stereo(hcpp::i16 *dst, const hcpp::i16 *src, hcpp::u32 frames) noexcept;
    void pcm_swap_channels(hcpp::i16 *stereo, hcpp::u32 frames) noexcept;

}  // namespace hype::audio
