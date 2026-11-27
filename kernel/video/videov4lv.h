#pragma once
#include "hcpp/types.hpp"
#include "kernel/video/videoframe.h"
namespace hype::video {
    enum class VideoDeviceCapability : hcpp::u32 {
        None = 0,
        Capture = 1,
        Output = 2,
        Streaming = 4,
        HardwareDecode = 8,
        Virtual = 16
    };
    struct VideoDeviceInfo {
        hcpp::u32 id;
        hcpp::u32 capabilities;
        hcpp::u32 width, height;
        PixelFormat format;
    };
    struct VideoStream;
    void v4lv_initialize() noexcept;
    bool v4lv_register_device(const VideoDeviceInfo&) noexcept;
    bool v4lv_unregister(hcpp::u32) noexcept;
    hcpp::size_t v4lv_device_count() noexcept;
    const VideoDeviceInfo* v4lv_device(hcpp::size_t) noexcept;
    const VideoDeviceInfo* v4lv_device_by_id(hcpp::u32) noexcept;
    VideoStream* v4lv_stream(hcpp::u32) noexcept;
    bool v4lv_req_bufs(hcpp::u32, hcpp::size_t) noexcept;
}  // namespace hype::video
