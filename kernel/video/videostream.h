#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video {
    constexpr hcpp::size_t video_stream_max_buffers = 8;
    struct VideoStreamBuffer {
        hcpp::u32 index;
        hcpp::size_t bytes_used;
        bool queued, dequeued;
        hcpp::u64 timestamp;
    };
    struct VideoStream {
        hcpp::u32 device_id;
        VideoStreamBuffer buffers[video_stream_max_buffers];
        hcpp::size_t count;
        bool streaming;
    };
    bool stream_initialize(VideoStream&, hcpp::u32, hcpp::size_t) noexcept;
    bool stream_start(VideoStream&) noexcept;
    bool stream_stop(VideoStream&) noexcept;
    bool stream_queue(VideoStream&, hcpp::u32, hcpp::size_t) noexcept;
    VideoStreamBuffer* stream_dequeue(VideoStream&) noexcept;
}  // namespace hype::video
