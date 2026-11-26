#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    struct ExifInfo {
        hcpp::u16 tag_count;
        hcpp::u16 orientation;
        char make[32];
        char model[32];
        char datetime[20];
    };

    CodecStatus exif_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                            Arena &arena) noexcept;

}  // namespace hype::codec::image
