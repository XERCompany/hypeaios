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
        s.head = 0;
        s.pending = 0;
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
    bool stream_push(VideoStream& s, const VideoFrame& f) noexcept {
        if (!s.streaming || !frame_valid(f)) return false;
        if (s.pending == video_stream_max_buffers) {
            s.head = (s.head + 1) % video_stream_max_buffers;
            --s.pending;
            ++s.dropped;
        }
        s.frames[(s.head + s.pending) % video_stream_max_buffers] = f;
        ++s.pending;
        return true;
    }
    bool stream_pop(VideoStream& s, VideoFrame& f) noexcept {
        if (!s.streaming || s.pending == 0) return false;
        f = s.frames[s.head];
        s.head = (s.head + 1) % video_stream_max_buffers;
        --s.pending;
        return true;
    }
    hcpp::size_t stream_pending(const VideoStream& s) noexcept { return s.pending; }
}  // namespace hype::video
