#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec {

    // AVI (RIFF) container decode: walks hdrl/movi via the video demuxer and
    // decodes the first video packet it can handle (MJPG -> jpeg_decode) into
    // a FrameBuf. Other stream codecs return Unsupported.
    CodecStatus avi_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;

    // Counts video packets in the movi list without decoding them.
    hcpp::u32 avi_frame_count(const hcpp::u8 *input, hcpp::size_t input_size) noexcept;

}  // namespace hype::codec
