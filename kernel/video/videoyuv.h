#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video { hcpp::u8 yuv_clamp(int)noexcept; bool yuv420_to_rgb24(const VideoFrame&,VideoFrame&)noexcept; }
