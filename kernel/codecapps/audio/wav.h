#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioparse.h"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    CodecStatus audio_wav_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                                 Arena &arena) noexcept;
    bool audio_wav_query_tags(const hcpp::u8 *input, hcpp::size_t input_size,
                              audio::WavTags &tags) noexcept;

}  // namespace hype::codec
