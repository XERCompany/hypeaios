#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"

namespace hype::audio {

    struct AudioClient {
        hcpp::u32 id;
        bool active;
        AudioStreamId stream;
        char name[16];
    };

    hcpp::u32 audio_client_open(const char *name) noexcept;
    bool audio_client_bind_stream(hcpp::u32 client, AudioStreamId stream) noexcept;
    bool audio_client_close(hcpp::u32 client) noexcept;
    const AudioClient *audio_client(hcpp::u32 client) noexcept;
    hcpp::u32 audio_client_count() noexcept;

}  // namespace hype::audio
