#include "kernel/video/videocontrol.h"
namespace hype::video {
    namespace {
        hcpp::i32 values[8][8];
        bool present[8][8];
    }  // namespace
    bool control_set(hcpp::u32 d, VideoControlId c, hcpp::i32 v) noexcept {
        hcpp::u32 i = static_cast<hcpp::u32>(c);
        if (d >= 8 || i == 0 || i >= 8) return false;
        values[d][i] = v;
        present[d][i] = true;
        return true;
    }
    bool control_get(hcpp::u32 d, VideoControlId c, hcpp::i32& v) noexcept {
        hcpp::u32 i = static_cast<hcpp::u32>(c);
        if (d >= 8 || i == 0 || i >= 8 || !present[d][i]) return false;
        v = values[d][i];
        return true;
    }
}  // namespace hype::video
