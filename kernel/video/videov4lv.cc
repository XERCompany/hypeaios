#include "kernel/video/videov4lv.h"

#include "kernel/video/videostream.h"
namespace hype::video {
    namespace {
        VideoDeviceInfo devices[8];
        VideoStream streams[8];
        hcpp::size_t count;
        hcpp::size_t index_of(hcpp::u32 id) noexcept {
            for (hcpp::size_t i = 0; i < count; ++i)
                if (devices[i].id == id) return i;
            return 8;
        }
    }  // namespace
    void v4lv_initialize() noexcept {
        count = 0;
        for (hcpp::size_t i = 0; i < 8; ++i) streams[i] = {};
    }
    bool v4lv_register_device(const VideoDeviceInfo& d) noexcept {
        if (count >= 8 || d.id >= 8 || index_of(d.id) != 8) return false;
        devices[count] = d;
        streams[count] = {};
        ++count;
        return true;
    }
    bool v4lv_unregister(hcpp::u32 id) noexcept {
        const hcpp::size_t i = index_of(id);
        if (i == 8) return false;
        for (hcpp::size_t j = i; j + 1 < count; ++j) {
            devices[j] = devices[j + 1];
            streams[j] = streams[j + 1];
        }
        --count;
        return true;
    }
    hcpp::size_t v4lv_device_count() noexcept { return count; }
    const VideoDeviceInfo* v4lv_device(hcpp::size_t i) noexcept { return i < count ? &devices[i] : nullptr; }
    const VideoDeviceInfo* v4lv_device_by_id(hcpp::u32 id) noexcept {
        const hcpp::size_t i = index_of(id);
        return i != 8 ? &devices[i] : nullptr;
    }
    VideoStream* v4lv_stream(hcpp::u32 id) noexcept {
        const hcpp::size_t i = index_of(id);
        return i != 8 ? &streams[i] : nullptr;
    }
    bool v4lv_req_bufs(hcpp::u32 id, hcpp::size_t n) noexcept {
        VideoStream* s = v4lv_stream(id);
        if (s == nullptr || s->streaming) return false;
        return stream_initialize(*s, id, n);
    }
}  // namespace hype::video
