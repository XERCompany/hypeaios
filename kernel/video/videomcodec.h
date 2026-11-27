#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    constexpr hcpp::size_t mcodec_max_entries = 16;
    bool mcodec_register(CodecType, DecodeFn) noexcept;
    DecodeFn mcodec_find(CodecType) noexcept;
    DecodeResult mcodec_decode(CodecType, const hcpp::u8*, hcpp::size_t,
                               VideoFrame&) noexcept;
    hcpp::size_t mcodec_count() noexcept;
    hcpp::size_t mcodec_register_builtins() noexcept;
}  // namespace hype::video
