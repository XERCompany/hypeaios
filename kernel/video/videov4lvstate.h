#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    enum class VideoDeviceState : hcpp::u8 { Idle, Open, Streaming, Error };
    void v4lv_state_reset() noexcept;
    bool v4lv_set_state(hcpp::u32, VideoDeviceState) noexcept;
    VideoDeviceState v4lv_state(hcpp::u32) noexcept;
    const char* v4lv_state_name(VideoDeviceState) noexcept;
    bool v4lv_stream_on(hcpp::u32) noexcept;
    bool v4lv_stream_off(hcpp::u32) noexcept;
    bool v4lv_can_capture(hcpp::u32) noexcept;
}  // namespace hype::video
