#include "kernel/video/videoformat.h"

#include "kernel/video/videov4lv_key.h"
namespace hype::video {
    namespace {
        constexpr VideoFormatInfo table[] = {
            {PixelFormat::RGB24, v4lv_key_rgb3, 1, 24, "RGB24"},
            {PixelFormat::RGB32, v4lv_key_rgb4, 1, 32, "RGB32"},
            {PixelFormat::YUV420, v4lv_key_i420, 3, 12, "I420"},
            {PixelFormat::NV12, v4lv_key_nv12, 2, 12, "NV12"},
        };
    }  // namespace
    bool format_supported(PixelFormat f) noexcept { return format_find(f) != nullptr; }
    bool format_negotiate(VideoDeviceInfo& d, hcpp::u32 w, hcpp::u32 h, PixelFormat f) noexcept {
        if (!w || !h || !format_supported(f)) return false;
        d.width = w;
        d.height = h;
        d.format = f;
        return true;
    }
    const char* format_name(PixelFormat f) noexcept {
        const VideoFormatInfo* info = format_find(f);
        return info != nullptr ? info->name : "unknown";
    }
    hcpp::size_t format_table_count() noexcept { return sizeof(table) / sizeof(table[0]); }
    const VideoFormatInfo* format_info(hcpp::size_t i) noexcept {
        return i < format_table_count() ? &table[i] : nullptr;
    }
    const VideoFormatInfo* format_find(PixelFormat f) noexcept {
        for (hcpp::size_t i = 0; i < format_table_count(); ++i)
            if (table[i].format == f) return &table[i];
        return nullptr;
    }
}  // namespace hype::video
