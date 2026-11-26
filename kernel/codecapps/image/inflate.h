#pragma once

#include "hcpp/types.hpp"

namespace hype::codec {

    bool inflate_deflate(const hcpp::u8 *input, hcpp::size_t input_size,
                         hcpp::size_t &consumed, hcpp::u8 *output,
                         hcpp::size_t output_cap, hcpp::size_t &produced) noexcept;

    bool inflate_zlib(const hcpp::u8 *input, hcpp::size_t input_size, hcpp::u8 *output,
                      hcpp::size_t output_cap, hcpp::size_t &produced) noexcept;

    hcpp::u32 inflate_adler32(const hcpp::u8 *data, hcpp::size_t size) noexcept;

    hcpp::size_t deflate_stored(const hcpp::u8 *input, hcpp::size_t input_size,
                                hcpp::u8 *output, hcpp::size_t output_cap) noexcept;

    hcpp::size_t zlib_stored(const hcpp::u8 *input, hcpp::size_t input_size,
                             hcpp::u8 *output, hcpp::size_t output_cap) noexcept;

}  // namespace hype::codec
