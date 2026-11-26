#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    bool format_supported(PixelFormat) noexcept;
    bool format_negotiate(VideoDeviceInfo&, hcpp::u32, hcpp::u32, PixelFormat) noexcept;
    const char* format_name(PixelFormat) noexcept;
}  // namespace hype::video
