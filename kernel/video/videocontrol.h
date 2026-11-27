#pragma once
#include "hcpp/types.hpp"
namespace hype::video {
    enum class VideoControlId : hcpp::u32 { Brightness = 1, Contrast, Saturation, Hue, Exposure, FrameRate };
    struct VideoControlRange {
        hcpp::i32 minimum, maximum, default_value, step;
    };
    const VideoControlRange* control_range(VideoControlId) noexcept;
    hcpp::i32 control_clamp(VideoControlId, hcpp::i32) noexcept;
    bool control_set(hcpp::u32, VideoControlId, hcpp::i32) noexcept;
    bool control_get(hcpp::u32, VideoControlId, hcpp::i32&) noexcept;
}  // namespace hype::video
