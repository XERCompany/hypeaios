#include "kernel/video/videoyuv.h"
namespace hype::video {
    hcpp::u8 yuv_clamp(int v) noexcept { return v < 0 ? 0 : v > 255 ? 255 : static_cast<hcpp::u8>(v); }
    void yuv_to_rgb(hcpp::u8 y, hcpp::u8 u, hcpp::u8 v, hcpp::u8& r, hcpp::u8& g, hcpp::u8& b) noexcept {
        const int Y = y;
        const int U = u - 128;
        const int V = v - 128;
        r = yuv_clamp(Y + ((359 * V) >> 8));
        g = yuv_clamp(Y - ((88 * U + 183 * V) >> 8));
        b = yuv_clamp(Y + ((454 * U) >> 8));
    }
    void rgb_to_yuv(hcpp::u8 r, hcpp::u8 g, hcpp::u8 b, hcpp::u8& y, hcpp::u8& u, hcpp::u8& v) noexcept {
        const int R = r;
        const int G = g;
        const int B = b;
        y = yuv_clamp((77 * R + 150 * G + 29 * B + 128) >> 8);
        u = yuv_clamp(((-43 * R - 85 * G + 128 * B + 128) >> 8) + 128);
        v = yuv_clamp(((128 * R - 107 * G - 21 * B + 128) >> 8) + 128);
    }
    bool yuv420_to_rgb24(const VideoFrame& in, VideoFrame& out) noexcept {
        if (!frame_valid(in) || !frame_valid(out) || in.format != PixelFormat::YUV420 ||
            out.format != PixelFormat::RGB24 || in.width != out.width || in.height != out.height)
            return false;
        const hcpp::u8* u_plane = in.data + in.width * in.height;
        const hcpp::u8* v_plane = u_plane + in.width * in.height / 4;
        for (hcpp::u32 y = 0; y < in.height; ++y)
            for (hcpp::u32 x = 0; x < in.width; ++x) {
                const hcpp::size_t c = (y / 2) * (in.width / 2) + x / 2;
                hcpp::u8* p = out.data + (y * out.width + x) * 3;
                yuv_to_rgb(in.data[y * in.width + x], u_plane[c], v_plane[c], p[0], p[1], p[2]);
            }
        return true;
    }
    bool i420_to_rgb32(const VideoFrame& in, VideoFrame& out) noexcept {
        if (!frame_valid(in) || !frame_valid(out) || in.format != PixelFormat::YUV420 ||
            out.format != PixelFormat::RGB32 || in.width != out.width || in.height != out.height)
            return false;
        const hcpp::u8* u_plane = in.data + in.width * in.height;
        const hcpp::u8* v_plane = u_plane + in.width * in.height / 4;
        for (hcpp::u32 y = 0; y < in.height; ++y)
            for (hcpp::u32 x = 0; x < in.width; ++x) {
                const hcpp::size_t c = (y / 2) * (in.width / 2) + x / 2;
                hcpp::u8* p = out.data + (y * out.width + x) * 4;
                yuv_to_rgb(in.data[y * in.width + x], u_plane[c], v_plane[c], p[0], p[1], p[2]);
                p[3] = 0xFF;
            }
        return true;
    }
    bool nv12_to_rgb32(const VideoFrame& in, VideoFrame& out) noexcept {
        if (!frame_valid(in) || !frame_valid(out) || in.format != PixelFormat::NV12 ||
            out.format != PixelFormat::RGB32 || in.width != out.width || in.height != out.height)
            return false;
        const hcpp::u8* uv = in.data + in.width * in.height;
        for (hcpp::u32 y = 0; y < in.height; ++y)
            for (hcpp::u32 x = 0; x < in.width; ++x) {
                const hcpp::size_t c = (y / 2) * in.width + (x & ~1u);
                hcpp::u8* p = out.data + (y * out.width + x) * 4;
                yuv_to_rgb(in.data[y * in.width + x], uv[c], uv[c + 1], p[0], p[1], p[2]);
                p[3] = 0xFF;
            }
        return true;
    }
    bool rgb32_to_nv12(const VideoFrame& in, VideoFrame& out) noexcept {
        if (!frame_valid(in) || !frame_valid(out) || in.format != PixelFormat::RGB32 ||
            out.format != PixelFormat::NV12 || in.width != out.width || in.height != out.height ||
            (in.width & 1) || (in.height & 1))
            return false;
        hcpp::u8* uv = out.data + out.width * out.height;
        for (hcpp::u32 y = 0; y < in.height; ++y)
            for (hcpp::u32 x = 0; x < in.width; ++x) {
                const hcpp::u8* p = in.data + (y * in.width + x) * 4;
                hcpp::u8 yy, uu, vv;
                rgb_to_yuv(p[0], p[1], p[2], yy, uu, vv);
                out.data[y * out.width + x] = yy;
                if ((y & 1) == 0 && (x & 1) == 0) {
                    const hcpp::size_t c = (y / 2) * out.width + x;
                    uv[c] = uu;
                    uv[c + 1] = vv;
                }
            }
        return true;
    }
}  // namespace hype::video
