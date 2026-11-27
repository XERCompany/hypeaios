#include "kernel/video/videov4lvstate.h"

#include "kernel/video/videostream.h"
namespace hype::video {
    namespace {
        VideoDeviceState states[8];
        bool transition_ok(VideoDeviceState from, VideoDeviceState to) noexcept {
            if (from == to || to == VideoDeviceState::Error) return true;
            switch (from) {
                case VideoDeviceState::Idle:
                    return to == VideoDeviceState::Open;
                case VideoDeviceState::Open:
                    return to == VideoDeviceState::Streaming || to == VideoDeviceState::Idle;
                case VideoDeviceState::Streaming:
                    return to == VideoDeviceState::Open;
                case VideoDeviceState::Error:
                    return to == VideoDeviceState::Idle;
            }
            return false;
        }
    }  // namespace
    void v4lv_state_reset() noexcept {
        for (hcpp::size_t i = 0; i < 8; ++i) states[i] = VideoDeviceState::Idle;
    }
    bool v4lv_set_state(hcpp::u32 id, VideoDeviceState s) noexcept {
        if (id >= 8 || v4lv_device_by_id(id) == nullptr || !transition_ok(states[id], s)) return false;
        states[id] = s;
        return true;
    }
    VideoDeviceState v4lv_state(hcpp::u32 id) noexcept { return id < 8 ? states[id] : VideoDeviceState::Error; }
    const char* v4lv_state_name(VideoDeviceState s) noexcept {
        switch (s) {
            case VideoDeviceState::Idle:
                return "idle";
            case VideoDeviceState::Open:
                return "open";
            case VideoDeviceState::Streaming:
                return "streaming";
            case VideoDeviceState::Error:
                return "error";
        }
        return "unknown";
    }
    bool v4lv_stream_on(hcpp::u32 id) noexcept {
        if (v4lv_state(id) != VideoDeviceState::Open) return false;
        VideoStream* s = v4lv_stream(id);
        if (s == nullptr || !stream_start(*s)) return false;
        return v4lv_set_state(id, VideoDeviceState::Streaming);
    }
    bool v4lv_stream_off(hcpp::u32 id) noexcept {
        if (v4lv_state(id) != VideoDeviceState::Streaming) return false;
        if (VideoStream* s = v4lv_stream(id)) stream_stop(*s);
        return v4lv_set_state(id, VideoDeviceState::Open);
    }
    bool v4lv_can_capture(hcpp::u32 id) noexcept { return v4lv_state(id) == VideoDeviceState::Streaming; }
}  // namespace hype::video
