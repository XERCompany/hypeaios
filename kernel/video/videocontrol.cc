#include "kernel/video/videocontrol.h"
namespace hype::video {
    namespace {
        constexpr VideoControlRange ranges[8] = {
            {0, 0, 0, 0},
            {-128, 127, 0, 1},
            {0, 255, 128, 1},
            {0, 255, 128, 1},
            {-180, 180, 0, 1},
            {0, 10000, 156, 1},
            {1, 240, 30, 1},
            {0, 0, 0, 0},
        };
        hcpp::i32 values[8][8];
        bool present[8][8];
    }  // namespace
    const VideoControlRange* control_range(VideoControlId c) noexcept {
        const hcpp::u32 i = static_cast<hcpp::u32>(c);
        if (i == 0 || i >= 8 || ranges[i].maximum == ranges[i].minimum) return nullptr;
        return &ranges[i];
    }
    hcpp::i32 control_clamp(VideoControlId c, hcpp::i32 v) noexcept {
        const VideoControlRange* r = control_range(c);
        if (r == nullptr) return v;
        if (v < r->minimum) return r->minimum;
        if (v > r->maximum) return r->maximum;
        return v;
    }
    bool control_set(hcpp::u32 d, VideoControlId c, hcpp::i32 v) noexcept {
        const hcpp::u32 i = static_cast<hcpp::u32>(c);
        if (d >= 8 || control_range(c) == nullptr) return false;
        values[d][i] = control_clamp(c, v);
        present[d][i] = true;
        return true;
    }
    bool control_get(hcpp::u32 d, VideoControlId c, hcpp::i32& v) noexcept {
        const hcpp::u32 i = static_cast<hcpp::u32>(c);
        const VideoControlRange* r = control_range(c);
        if (d >= 8 || r == nullptr) return false;
        v = present[d][i] ? values[d][i] : r->default_value;
        return true;
    }
}  // namespace hype::video
