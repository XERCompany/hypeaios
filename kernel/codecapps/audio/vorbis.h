#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    CodecStatus audio_vorbis_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                    void *output, Arena &arena) noexcept;

}  // namespace hype::codec
