#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    bool raw_frame_accept(const VideoFrame&) noexcept;
    bool raw_frame_clear(VideoFrame&) noexcept;
    bool raw_frame_unpack(const hcpp::u8*, hcpp::size_t, hcpp::u32, hcpp::u32, PixelFormat,
                          VideoFrame&) noexcept;
    DecodeResult raw_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
}  // namespace hype::video
