#include "kernel/video/videodecoderlz.h"
namespace hype::video {
    namespace {
        hcpp::u16 lzf_rd16(const hcpp::u8* p) noexcept {
            return static_cast<hcpp::u16>(p[0] | (static_cast<hcpp::u16>(p[1]) << 8));
        }
        void lzf_wr16(hcpp::u8* p, hcpp::u16 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v & 0xFF);
            p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
        }
        hcpp::u32 lzf_row_bytes(hcpp::u32 w, PixelFormat f) noexcept {
            if (f == PixelFormat::RGB24) return w * 3;
            if (f == PixelFormat::RGB32) return w * 4;
            if (f == PixelFormat::YUY2) return w * 2;
            return 0;
        }
    }
    bool lzf_probe(const hcpp::u8* src, hcpp::size_t n) noexcept {
        return n >= 10 && src[0] == 'L' && src[1] == 'Z' && src[2] == 'F' && src[3] == '1';
    }
    hcpp::size_t lzf_encode_frame(const VideoFrame& in, hcpp::u8* dst, hcpp::size_t cap) noexcept {
        if (!frame_valid(in) || dst == nullptr) return 0;
        const hcpp::u32 rb = lzf_row_bytes(in.width, in.format);
        if (rb == 0 || in.pitch < rb || in.width > 0xFFFF || in.height > 0xFFFF) return 0;
        const hcpp::size_t total = hcpp::size_t(rb) * in.height;
        if (cap < 10) return 0;
        dst[0] = 'L';
        dst[1] = 'Z';
        dst[2] = 'F';
        dst[3] = '1';
        lzf_wr16(dst + 4, static_cast<hcpp::u16>(in.width));
        lzf_wr16(dst + 6, static_cast<hcpp::u16>(in.height));
        dst[8] = static_cast<hcpp::u8>(in.format);
        dst[9] = 0;
        auto delta_at = [&](hcpp::size_t i) noexcept -> hcpp::u8 {
            const hcpp::size_t row = i / rb;
            const hcpp::size_t col = i % rb;
            const hcpp::u8 v = in.data[row * in.pitch + col];
            if (row == 0) return v;
            return static_cast<hcpp::u8>(v - in.data[(row - 1) * in.pitch + col]);
        };
        hcpp::size_t o = 10, pos = 0;
        while (pos < total) {
            const hcpp::u8 v = delta_at(pos);
            hcpp::size_t run = 1;
            while (pos + run < total && run < 128 && delta_at(pos + run) == v) ++run;
            if (run >= 3) {
                if (o + 2 > cap) return 0;
                dst[o++] = static_cast<hcpp::u8>(run - 1);
                dst[o++] = v;
                pos += run;
                continue;
            }
            const hcpp::size_t lit_start = pos;
            hcpp::size_t lit = 0;
            while (pos < total && lit < 128) {
                const hcpp::u8 lv = delta_at(pos);
                hcpp::size_t ahead = 1;
                while (pos + ahead < total && ahead < 3 && delta_at(pos + ahead) == lv) ++ahead;
                if (ahead >= 3) break;
                ++lit;
                ++pos;
            }
            if (lit == 0) continue;
            if (o + 1 + lit > cap) return 0;
            dst[o++] = static_cast<hcpp::u8>(0x80 | (lit - 1));
            for (hcpp::size_t k = 0; k < lit; ++k) dst[o++] = delta_at(lit_start + k);
        }
        return o;
    }
    DecodeResult lz_decode_frame(const hcpp::u8* src, hcpp::size_t n, VideoFrame& out) noexcept {
        if (!lzf_probe(src, n) || out.data == nullptr)
            return {false, 0, 0, CodecType::Lz};
        const hcpp::u32 w = lzf_rd16(src + 4);
        const hcpp::u32 h = lzf_rd16(src + 6);
        const PixelFormat fmt = static_cast<PixelFormat>(src[8]);
        const hcpp::u32 rb = lzf_row_bytes(w, fmt);
        if (rb == 0) return {false, 10, 0, CodecType::Lz};
        const hcpp::size_t total = hcpp::size_t(rb) * h;
        if (out.size < total) return {false, 10, 0, CodecType::Lz};
        out.width = w;
        out.height = h;
        out.pitch = rb;
        out.format = fmt;
        auto put = [&](hcpp::size_t i, hcpp::u8 v) noexcept {
            const hcpp::size_t row = i / rb;
            const hcpp::size_t col = i % rb;
            if (row == 0) {
                out.data[col] = v;
            } else {
                out.data[row * out.pitch + col] =
                    static_cast<hcpp::u8>(v + out.data[(row - 1) * out.pitch + col]);
            }
        };
        hcpp::size_t i = 10, o = 0;
        while (o < total) {
            if (i >= n) return {false, i, o, CodecType::Lz};
            const hcpp::u8 c = src[i++];
            if (c & 0x80) {
                const hcpp::size_t lit = (c & 0x7F) + 1;
                if (i + lit > n || o + lit > total) return {false, i, o, CodecType::Lz};
                for (hcpp::size_t k = 0; k < lit; ++k) put(o + k, src[i + k]);
                i += lit;
                o += lit;
            } else {
                const hcpp::size_t run = c + 1;
                if (i >= n || o + run > total) return {false, i, o, CodecType::Lz};
                const hcpp::u8 v = src[i++];
                for (hcpp::size_t k = 0; k < run; ++k) put(o + k, v);
                o += run;
            }
        }
        out.size = total;
        return {true, i, o, CodecType::Lz};
    }
    bool ffv1_probe(const hcpp::u8* src, hcpp::size_t n) noexcept {
        return n >= 4 && src[0] == 'F' && src[1] == 'F' && src[2] == 'V' && src[3] == '1';
    }
    DecodeResult ffv1_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept {
        return {false, 0, 0, CodecType::Ffv1};
    }
}  // namespace hype::video
