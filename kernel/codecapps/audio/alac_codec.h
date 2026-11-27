#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    CodecStatus audio_alac_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept;
    CodecStatus audio_alac_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                                  hcpp::size_t &written, Arena &arena) noexcept;

}  // namespace hype::codec
