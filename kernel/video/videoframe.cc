#include "kernel/video/videoframe.h"
namespace hype::video {
    hcpp::size_t frame_expected_size(hcpp::u32 w, hcpp::u32 h, PixelFormat f) noexcept {
        if (f == PixelFormat::RGB24) return hcpp::size_t(w) * h * 3;
        if (f == PixelFormat::RGB32) return hcpp::size_t(w) * h * 4;
        if (f == PixelFormat::YUV420 || f == PixelFormat::NV12) return hcpp::size_t(w) * h * 3 / 2;
        return 0;
    }
    bool frame_valid(const VideoFrame& f) noexcept {
        return f.width && f.height && f.data && f.size >= frame_expected_size(f.width, f.height, f.format) &&
               frame_expected_size(f.width, f.height, f.format) != 0;
    }
}  // namespace hype::video
