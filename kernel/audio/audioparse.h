#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    struct WavInfo {
        AudioFormat format;
        hcpp::u16 audio_format_tag;
        const void *sample_data;
        hcpp::u32 sample_bytes;
    };

    inline hcpp::u32 audio_tag(const char *four) noexcept {
        return static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[0])) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[1])) << 8) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[2])) << 16) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(four[3])) << 24);
    }

    bool wav_parse(const void *buffer, hcpp::u32 size, WavInfo &info) noexcept;

}  // namespace hype::audio
