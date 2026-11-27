#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/alac.h"

namespace hype::audio {

    struct AlacEncoderState {
        AlacConfig config;
        bool initialized;
        hcpp::u32 frames_encoded;
    };

    inline bool alac_encoder_supported() noexcept { return true; }

    bool alac_encoder_initialize(AlacEncoderState &state, const AlacConfig &config) noexcept;
    void alac_encoder_reset(AlacEncoderState &state) noexcept;
    hcpp::u32 alac_encoder_encode(AlacEncoderState &state, const hcpp::i16 *frames, hcpp::u32 frame_count,
                                  void *packet, hcpp::u32 packet_capacity) noexcept;

    inline hcpp::u32 alac_encode_pcm(const AlacConfig &config, const hcpp::i16 *frames,
                                     hcpp::u32 frame_count, void *packet,
                                     hcpp::u32 packet_capacity) noexcept {
        AlacEncoderState state{};
        if (!alac_encoder_initialize(state, config)) {
            return 0;
        }
        return alac_encoder_encode(state, frames, frame_count, packet, packet_capacity);
    }

}  // namespace hype::audio
