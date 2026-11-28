#pragma once

#include "kernel/codecapps/archive/archive.h"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    // WOFF 1.0 wrapper (wave-2): sfnt tables individually zlib/stored —
    // decode reconstructs a plain TTF/OTF byte stream into archive::BlobOut
    // so kernel/font/ttf.cc can parse it as usual. WOFF2 (brotli) stays XL.
    CodecStatus woff_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                            void *output, Arena &arena) noexcept;

}  // namespace hype::codec
