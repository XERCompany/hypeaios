#pragma once

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    CodecStatus png_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;
    CodecStatus png_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &arena) noexcept;
    bool png_selftest() noexcept;

}  // namespace hype::codec
