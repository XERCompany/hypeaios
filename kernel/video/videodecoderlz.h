#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    DecodeResult lz_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
}
