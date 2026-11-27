#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    enum class AudioCodecId : hcpp::u8 {
        Pcm,
        WavPcm,
        Alac,
        Mp2,
        Ac3,
        Flac,
        Vorbis,
        Opus,
        Aac,
        Other
    };

    struct AudioDecoderEntry {
        AudioCodecId codec;
        const char *name;
        bool (*probe)(const void *data, hcpp::u32 bytes) noexcept;
    };

    bool dvdeca_register(const AudioDecoderEntry &entry) noexcept;
    const AudioDecoderEntry *dvdeca_find(AudioCodecId codec) noexcept;
    hcpp::size_t dvdeca_count() noexcept;
    void dvdeca_register_builtin() noexcept;

}  // namespace hype::audio
