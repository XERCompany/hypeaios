#include "kernel/video/videoclient.h"

#include "kernel/video/videocapture.h"
#include "kernel/video/videostream.h"
#include "kernel/video/videov4lvstate.h"
#include "kernel/video/videovirtual.h"
namespace hype::video {
    namespace {
        struct VideoClient {
            bool used;
            hcpp::u32 device_id;
        };
        VideoClient clients[video_client_max];
        VideoClient* client_at(hcpp::i32 handle) noexcept {
            if (handle < 0 || static_cast<hcpp::size_t>(handle) >= video_client_max ||
                !clients[handle].used)
                return nullptr;
            return &clients[handle];
        }
    }  // namespace
    bool video_client_initialize() noexcept {
        v4lv_initialize();
        v4lv_state_reset();
        for (hcpp::size_t i = 0; i < video_client_max; ++i) clients[i] = {};
        return true;
    }
    bool video_client_open(hcpp::u32 id) noexcept {
        if (v4lv_device_by_id(id) == nullptr) return false;
        if (v4lv_state(id) == VideoDeviceState::Open) return true;
        return v4lv_set_state(id, VideoDeviceState::Open);
    }
    bool video_client_close(hcpp::u32 id) noexcept {
        return v4lv_set_state(id, VideoDeviceState::Idle);
    }
    hcpp::i32 video_open_dev(hcpp::u32 id) noexcept {
        const VideoDeviceInfo* d = v4lv_device_by_id(id);
        if (d == nullptr) return -1;
        if (d->capabilities & static_cast<hcpp::u32>(VideoDeviceCapability::Virtual)) {
            if (!vcap_open(id)) return -1;
        } else if (v4lv_state(id) == VideoDeviceState::Idle) {
            if (!v4lv_set_state(id, VideoDeviceState::Open)) return -1;
        } else if (v4lv_state(id) != VideoDeviceState::Open) {
            return -1;
        }
        for (hcpp::size_t i = 0; i < video_client_max; ++i)
            if (!clients[i].used) {
                clients[i].used = true;
                clients[i].device_id = id;
                return static_cast<hcpp::i32>(i);
            }
        return -1;
    }
    bool video_req_bufs(hcpp::i32 handle, hcpp::size_t n) noexcept {
        const VideoClient* c = client_at(handle);
        return c != nullptr && v4lv_req_bufs(c->device_id, n);
    }
    bool video_qbuf(hcpp::i32 handle) noexcept {
        const VideoClient* c = client_at(handle);
        if (c == nullptr || !v4lv_can_capture(c->device_id)) return false;
        VideoFrame frame{};
        if (!capture_read_frame(c->device_id, frame)) return false;
        return stream_push(*v4lv_stream(c->device_id), frame);
    }
    bool video_dqbuf(hcpp::i32 handle, VideoFrame& out) noexcept {
        const VideoClient* c = client_at(handle);
        if (c == nullptr || !v4lv_can_capture(c->device_id)) return false;
        return stream_pop(*v4lv_stream(c->device_id), out);
    }
    bool video_close_dev(hcpp::i32 handle) noexcept {
        VideoClient* c = client_at(handle);
        if (c == nullptr) return false;
        const hcpp::u32 id = c->device_id;
        c->used = false;
        if (v4lv_state(id) == VideoDeviceState::Streaming) v4lv_stream_off(id);
        return v4lv_set_state(id, VideoDeviceState::Idle);
    }
}  // namespace hype::video
