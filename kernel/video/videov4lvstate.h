#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    enum class VideoDeviceState : hcpp::u8 { Stopped, Open, Streaming, Error };
    bool v4lv_set_state(hcpp::u32, VideoDeviceState) noexcept;
    VideoDeviceState v4lv_state(hcpp::u32) noexcept;
}  // namespace hype::video
