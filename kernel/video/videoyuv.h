#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video {
    hcpp::u8 yuv_clamp(int) noexcept;
    void yuv_to_rgb(hcpp::u8, hcpp::u8, hcpp::u8, hcpp::u8&, hcpp::u8&, hcpp::u8&) noexcept;
    void rgb_to_yuv(hcpp::u8, hcpp::u8, hcpp::u8, hcpp::u8&, hcpp::u8&, hcpp::u8&) noexcept;
    bool yuv420_to_rgb24(const VideoFrame&, VideoFrame&) noexcept;
    bool i420_to_rgb32(const VideoFrame&, VideoFrame&) noexcept;
    bool nv12_to_rgb32(const VideoFrame&, VideoFrame&) noexcept;
    bool rgb32_to_nv12(const VideoFrame&, VideoFrame&) noexcept;
}  // namespace hype::video
