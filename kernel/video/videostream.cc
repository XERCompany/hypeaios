#include "kernel/video/videostream.h"
namespace hype::video {
    bool stream_initialize(VideoStream& s, hcpp::u32 id, hcpp::size_t n) noexcept {
        if (n > video_stream_max_buffers) return false;
        s = {};
        s.device_id = id;
        s.count = n;
        for (hcpp::size_t i = 0; i < n; ++i) s.buffers[i].index = i;
        return true;
    }
    bool stream_start(VideoStream& s) noexcept {
        if (!s.count) return false;
        s.streaming = true;
        return true;
    }
    bool stream_stop(VideoStream& s) noexcept {
        s.streaming = false;
        return true;
    }
    bool stream_queue(VideoStream& s, hcpp::u32 i, hcpp::size_t bytes) noexcept {
        if (i >= s.count) return false;
        auto& b = s.buffers[i];
        b.bytes_used = bytes;
        b.queued = true;
        b.dequeued = false;
        return true;
    }
    VideoStreamBuffer* stream_dequeue(VideoStream& s) noexcept {
        if (!s.streaming) return nullptr;
        for (hcpp::size_t i = 0; i < s.count; ++i)
            if (s.buffers[i].queued && !s.buffers[i].dequeued) {
                s.buffers[i].dequeued = true;
                return &s.buffers[i];
            }
        return nullptr;
    }
}  // namespace hype::video
