#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    constexpr hcpp::u32 wav_tag_text_max = 64;

    struct WavTags {
        char title[wav_tag_text_max];
        char artist[wav_tag_text_max];
        char comment[wav_tag_text_max];
        hcpp::u32 tag_count;
    };

    struct WavInfo {
        AudioFormat format;
        WavTags tags;
        hcpp::u16 audio_format_tag;
        hcpp::u16 bits_per_sample;
        const void *sample_data;
        hcpp::u32 data_offset;
        hcpp::u32 sample_bytes;
    };

    struct RiffChunk {
        hcpp::u32 fourcc;
        hcpp::u32 size;
        hcpp::u32 data_offset;
    };

    struct RiffWalker {
        const hcpp::u8 *base;
        hcpp::u32 offset;
        hcpp::u32 end;
    };

    inline hcpp::u32 audio_tag(const char *four) noexcept {
        return static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[0])) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[1])) << 8) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[2])) << 16) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[3])) << 24);
    }

    bool riff_walk_begin(RiffWalker &walker, const void *data, hcpp::u32 size) noexcept;
    bool riff_next_chunk(RiffWalker &walker, RiffChunk &chunk) noexcept;
    bool wav_parse(const void *buffer, hcpp::u32 size, WavInfo &info) noexcept;

}  // namespace hype::audio
