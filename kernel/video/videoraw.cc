#include "kernel/video/videoraw.h"
namespace hype::video {
    bool raw_frame_accept(const VideoFrame& f) noexcept {
        return frame_valid(f) && (f.format == PixelFormat::RGB24 || f.format == PixelFormat::RGB32);
    }
    bool raw_frame_clear(VideoFrame& f) noexcept {
        if (!raw_frame_accept(f)) return false;
        for (hcpp::size_t i = 0; i < f.size; ++i) f.data[i] = 0;
        return true;
    }
}  // namespace hype::video
