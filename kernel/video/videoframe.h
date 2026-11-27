#pragma once
#include "hcpp/types.hpp"
namespace hype::video {
    enum class PixelFormat : hcpp::u8 { Unknown, RGB24, RGB32, YUV420, NV12, YUY2, Compressed };
    struct VideoFrame {
        hcpp::u32 width, height, pitch;
        PixelFormat format;
        hcpp::u8* data;
        hcpp::size_t size;
        hcpp::u64 timestamp;
    };
    bool frame_valid(const VideoFrame&) noexcept;
    hcpp::size_t frame_expected_size(hcpp::u32, hcpp::u32, PixelFormat) noexcept;
}  // namespace hype::video
