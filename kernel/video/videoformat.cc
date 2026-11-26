#include "kernel/video/videoformat.h"
namespace hype::video {
    bool format_supported(PixelFormat f) noexcept {
        return f == PixelFormat::RGB24 || f == PixelFormat::RGB32 || f == PixelFormat::YUV420 || f == PixelFormat::NV12;
    }
    bool format_negotiate(VideoDeviceInfo& d, hcpp::u32 w, hcpp::u32 h, PixelFormat f) noexcept {
        if (!w || !h || !format_supported(f)) return false;
        d.width = w;
        d.height = h;
        d.format = f;
        return true;
    }
    const char* format_name(PixelFormat f) noexcept {
        switch (f) {
            case PixelFormat::RGB24:
                return "RGB24";
            case PixelFormat::RGB32:
                return "RGB32";
            case PixelFormat::YUV420:
                return "YUV420";
            case PixelFormat::NV12:
                return "NV12";
            default:
                return "unknown";
        }
    }
}  // namespace hype::video
