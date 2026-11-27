#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    struct VideoFormatInfo {
        PixelFormat format;
        hcpp::u32 fourcc;
        hcpp::u8 planes;
        hcpp::u8 bits_per_pixel;
        const char* name;
    };
    bool format_supported(PixelFormat) noexcept;
    bool format_negotiate(VideoDeviceInfo&, hcpp::u32, hcpp::u32, PixelFormat) noexcept;
    const char* format_name(PixelFormat) noexcept;
    hcpp::size_t format_table_count() noexcept;
    const VideoFormatInfo* format_info(hcpp::size_t) noexcept;
    const VideoFormatInfo* format_find(PixelFormat) noexcept;
}  // namespace hype::video
