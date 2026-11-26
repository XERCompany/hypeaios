#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/alac.h"

namespace hype::audio {

    struct AlacEncoderState {
        AlacConfig config;
        bool initialized;
        hcpp::u32 frames_encoded;
    };

    bool alac_encoder_initialize(AlacEncoderState &state, const AlacConfig &config) noexcept;
    void alac_encoder_reset(AlacEncoderState &state) noexcept;
    hcpp::u32 alac_encoder_encode(AlacEncoderState &state, const hcpp::i16 *frames, hcpp::u32 frame_count,
                                  void *packet, hcpp::u32 packet_capacity) noexcept;

}  // namespace hype::audio
