#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    constexpr hcpp::u32 vcap_width = 176;
    constexpr hcpp::u32 vcap_height = 144;
    constexpr hcpp::u32 vcap_bars = 8;
    bool virtual_video_register() noexcept;
    bool vcap_open(hcpp::u32) noexcept;
    bool vcap_frame(hcpp::u32, VideoFrame&) noexcept;
    hcpp::u32 vcap_frame_index() noexcept;
}
