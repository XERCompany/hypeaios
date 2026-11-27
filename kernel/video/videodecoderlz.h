#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    bool lzf_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    hcpp::size_t lzf_encode_frame(const VideoFrame&, hcpp::u8*, hcpp::size_t) noexcept;
    DecodeResult lz_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
    bool ffv1_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    DecodeResult ffv1_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
}  // namespace hype::video
