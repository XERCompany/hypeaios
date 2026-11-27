#include "kernel/video/videoraw.h"

#include "kernel/video/videoyuv.h"
namespace hype::video {
    namespace {
        hcpp::u32 raw_bpp(PixelFormat f) noexcept {
            if (f == PixelFormat::RGB24) return 3;
            if (f == PixelFormat::RGB32) return 4;
            if (f == PixelFormat::YUY2) return 2;
            return 0;
        }
        void raw_put(VideoFrame& out, hcpp::u32 x, hcpp::u32 y, hcpp::u8 r, hcpp::u8 g,
                     hcpp::u8 b) noexcept {
            hcpp::u8* p = out.data + hcpp::size_t(y) * out.pitch + hcpp::size_t(x) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 0xFF;
        }
        bool raw_copy_rows(const hcpp::u8* src, hcpp::size_t src_pitch, VideoFrame& out,
                           hcpp::u32 row_bytes) noexcept {
            for (hcpp::u32 y = 0; y < out.height; ++y) {
                hcpp::u8* d = out.data + hcpp::size_t(y) * out.pitch;
                const hcpp::u8* s = src + hcpp::size_t(y) * src_pitch;
                for (hcpp::u32 k = 0; k < row_bytes; ++k) d[k] = s[k];
            }
            return true;
        }
    }
    bool raw_frame_accept(const VideoFrame& f) noexcept {
        return frame_valid(f) && (f.format == PixelFormat::RGB24 || f.format == PixelFormat::RGB32);
    }
    bool raw_frame_clear(VideoFrame& f) noexcept {
        if (!raw_frame_accept(f)) return false;
        for (hcpp::size_t i = 0; i < f.size; ++i) f.data[i] = 0;
        return true;
    }
    bool raw_frame_unpack(const hcpp::u8* src, hcpp::size_t n, hcpp::u32 w, hcpp::u32 h,
                          PixelFormat fmt, VideoFrame& out) noexcept {
        if (src == nullptr || out.data == nullptr || w == 0 || h == 0) return false;
        if (fmt == PixelFormat::YUV420 || fmt == PixelFormat::NV12) {
            const hcpp::size_t need = hcpp::size_t(w) * h * 3 / 2;
            if (n < need) return false;
            if (out.format == fmt) {
                if (out.size < need) return false;
                out.width = w;
                out.height = h;
                out.pitch = w;
                for (hcpp::size_t k = 0; k < need; ++k) out.data[k] = src[k];
                out.size = need;
                return true;
            }
            if (out.format != PixelFormat::RGB24) return false;
            VideoFrame in{w, h, w, fmt, const_cast<hcpp::u8*>(src), need, 0};
            out.width = w;
            out.height = h;
            out.pitch = w * 3;
            if (out.size < frame_expected_size(w, h, PixelFormat::RGB24)) return false;
            return yuv420_to_rgb24(in, out);
        }
        const hcpp::u32 bpp = raw_bpp(fmt);
        if (bpp == 0) return false;
        const hcpp::size_t need = hcpp::size_t(w) * bpp * h;
        if (n < need) return false;
        if (out.format == fmt && (fmt == PixelFormat::RGB24 || fmt == PixelFormat::RGB32)) {
            const hcpp::u32 row = w * bpp;
            if (out.size < hcpp::size_t(row) * h) return false;
            out.width = w;
            out.height = h;
            out.pitch = row;
            raw_copy_rows(src, row, out, row);
            out.size = hcpp::size_t(row) * h;
            return true;
        }
        if (fmt == PixelFormat::YUY2 && out.format == PixelFormat::RGB32) {
            if (out.size < hcpp::size_t(w) * 4 * h) return false;
            out.width = w;
            out.height = h;
            out.pitch = w * 4;
            for (hcpp::u32 y = 0; y < h; ++y) {
                const hcpp::u8* s = src + hcpp::size_t(y) * w * 2;
                for (hcpp::u32 x = 0; x + 1 < w; x += 2) {
                    const int Y0 = s[x * 2];
                    const int U = s[x * 2 + 1] - 128;
                    const int Y1 = s[x * 2 + 2];
                    const int V = s[x * 2 + 3] - 128;
                    raw_put(out, x, y, yuv_clamp(Y0 + ((359 * V) >> 8)),
                            yuv_clamp(Y0 - ((88 * U + 183 * V) >> 8)),
                            yuv_clamp(Y0 + ((454 * U) >> 8)));
                    raw_put(out, x + 1, y, yuv_clamp(Y1 + ((359 * V) >> 8)),
                            yuv_clamp(Y1 - ((88 * U + 183 * V) >> 8)),
                            yuv_clamp(Y1 + ((454 * U) >> 8)));
                }
            }
            out.size = hcpp::size_t(w) * 4 * h;
            return true;
        }
        return false;
    }
    DecodeResult raw_decode_frame(const hcpp::u8* src, hcpp::size_t n, VideoFrame& out) noexcept {
        if (src == nullptr || out.data == nullptr || out.width == 0 || out.height == 0)
            return {false, 0, 0, CodecType::Raw};
        const hcpp::size_t need = frame_expected_size(out.width, out.height, out.format);
        if (need == 0 || n < need || out.size < need)
            return {false, n, 0, CodecType::Raw};
        if (out.format == PixelFormat::YUV420 || out.format == PixelFormat::NV12) {
            out.pitch = out.width;
            for (hcpp::size_t k = 0; k < need; ++k) out.data[k] = src[k];
            out.size = need;
            return {true, need, need, CodecType::Raw};
        }
        const hcpp::u32 bpp = raw_bpp(out.format);
        if (bpp == 0) return {false, n, 0, CodecType::Raw};
        const hcpp::u32 row = out.width * bpp;
        if (out.pitch < row) out.pitch = row;
        if (out.size < hcpp::size_t(out.pitch) * (out.height - 1) + row)
            return {false, n, 0, CodecType::Raw};
        raw_copy_rows(src, row, out, row);
        out.size = hcpp::size_t(out.pitch) * out.height;
        return {true, need, out.size, CodecType::Raw};
    }
}  // namespace hype::video
