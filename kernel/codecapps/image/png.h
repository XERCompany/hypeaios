#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    CodecStatus png_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;
    CodecStatus png_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &arena) noexcept;

    // Wave-2 (APNG): runs the scanline pipeline for one already-located
    // zlib stream — inflate + unfilter + color-expand — writing packed
    // RGBA (width*4 stride) into `rgba` (rgba_cap >= w*h*4). The caller
    // supplies IHDR fields; used by apng.cc per animation frame.
    CodecStatus png_pixels_decode(hcpp::u32 width, hcpp::u32 height,
                                  hcpp::u8 color_type, const hcpp::u8 *palette,
                                  hcpp::size_t palette_size, const hcpp::u8 *zdata,
                                  hcpp::size_t zdata_size, hcpp::u8 *rgba,
                                  hcpp::size_t rgba_cap, Arena &arena) noexcept;

    bool png_selftest() noexcept;

}  // namespace hype::codec::image
