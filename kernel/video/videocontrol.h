#pragma once
#include "hcpp/types.hpp"
namespace hype::video {
    enum class VideoControlId : hcpp::u32 { Brightness = 1, Contrast, Saturation, Hue, Exposure, FrameRate };
    bool control_set(hcpp::u32, VideoControlId, hcpp::i32) noexcept;
    bool control_get(hcpp::u32, VideoControlId, hcpp::i32&) noexcept;
}  // namespace hype::video
