#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/alac.h"

namespace hype::audio {

    struct AlacDecoderState {
        AlacConfig config;
        bool initialized;
        hcpp::u32 frames_decoded;
    };

    bool alac_decoder_initialize(AlacDecoderState &state, const AlacConfig &config) noexcept;
    void alac_decoder_reset(AlacDecoderState &state) noexcept;
    hcpp::u32 alac_decoder_decode(AlacDecoderState &state, const void *packet, hcpp::u32 packet_bytes,
                                  hcpp::i16 *output, hcpp::u32 max_frames) noexcept;

}  // namespace hype::audio
