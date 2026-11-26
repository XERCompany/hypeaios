#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    bool video_client_initialize() noexcept;
    bool video_client_open(hcpp::u32) noexcept;
    bool video_client_close(hcpp::u32) noexcept;
}  // namespace hype::video
