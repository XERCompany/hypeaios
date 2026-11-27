#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video {
    bool capture_copy_frame(const VideoFrame&, VideoFrame&) noexcept;
    bool capture_convert_frame(const VideoFrame&, VideoFrame&) noexcept;
    bool capture_read_frame(hcpp::u32, VideoFrame&) noexcept;
}  // namespace hype::video
