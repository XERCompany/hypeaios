#include "kernel/video/videocapture.h"

#include "kernel/video/videov4lv.h"
#include "kernel/video/videovirtual.h"
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
        if (src.format == PixelFormat::YUV420 && dst.format == PixelFormat::RGB24)
            return yuv420_to_rgb24(src, dst);
        if (src.format == PixelFormat::YUV420 && dst.format == PixelFormat::RGB32)
            return i420_to_rgb32(src, dst);
        if (src.format == PixelFormat::NV12 && dst.format == PixelFormat::RGB32)
            return nv12_to_rgb32(src, dst);
        if (src.format == PixelFormat::RGB32 && dst.format == PixelFormat::NV12)
            return rgb32_to_nv12(src, dst);
        return false;
    }
    bool capture_read_frame(hcpp::u32 device_id, VideoFrame& out) noexcept {
        const VideoDeviceInfo* d = v4lv_device_by_id(device_id);
        if (d == nullptr ||
            !(d->capabilities & static_cast<hcpp::u32>(VideoDeviceCapability::Virtual)))
            return false;
        return vcap_frame(device_id, out);
    }
}  // namespace hype::video
