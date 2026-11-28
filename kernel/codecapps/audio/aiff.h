#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec {

    // AIFF (FORM/AIFF, Apple/SGI EA-IFF 85): COMM chunk gives
    // channels/frames/bits/rate (80-bit IEEE extended), SSND carries
    // big-endian PCM. AIFC 'sowt' (little-endian PCM) is also accepted.
    CodecStatus audio_aiff_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept;

}  // namespace hype::codec
