#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video {
    bool raw_frame_accept(const VideoFrame&) noexcept;
    bool raw_frame_clear(VideoFrame&) noexcept;
}  // namespace hype::video
