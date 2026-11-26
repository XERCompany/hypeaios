#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    bool tga_probe(const hcpp::u8 *input, hcpp::size_t input_size) noexcept;
    CodecStatus tga_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;
    CodecStatus tga_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &arena) noexcept;

}  // namespace hype::codec::image
