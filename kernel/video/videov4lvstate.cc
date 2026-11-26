#include "kernel/video/videov4lvstate.h"
namespace hype::video {
    namespace {
        VideoDeviceState states[8];
    }
    bool v4lv_set_state(hcpp::u32 id, VideoDeviceState s) noexcept {
        if (id >= 8) return false;
        states[id] = s;
        return true;
    }
    VideoDeviceState v4lv_state(hcpp::u32 id) noexcept { return id < 8 ? states[id] : VideoDeviceState::Error; }
}  // namespace hype::video
