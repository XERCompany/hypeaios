#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    CodecStatus jpeg_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                            Arena &arena) noexcept;
    bool jpeg_selftest() noexcept;

}  // namespace hype::codec::image
