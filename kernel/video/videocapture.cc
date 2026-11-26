#include "kernel/video/videocapture.h"

#include "kernel/video/videoyuv.h"
namespace hype::video {
    bool capture_copy_frame(const VideoFrame& src, VideoFrame& dst) noexcept {
        if (!frame_valid(src) || !frame_valid(dst) || src.format != dst.format || src.width != dst.width ||
            src.height != dst.height)
            return false;
        for (hcpp::size_t i = 0; i < src.size && i < dst.size; ++i) dst.data[i] = src.data[i];
        return true;
    }
    bool capture_convert_frame(const VideoFrame& src, VideoFrame& dst) noexcept {
        if (src.format == dst.format) return capture_copy_frame(src, dst);
        if (src.format == PixelFormat::YUV420 && dst.format == PixelFormat::RGB24) return yuv420_to_rgb24(src, dst);
        return false;
    }
}  // namespace hype::video
