#include "kernel/video/videocoretest.h"

#include "kernel/serial.hpp"
#include "kernel/video/videoclient.h"
#include "kernel/codecapps/image/jpeg_selftest_blob.h"
#include "kernel/serial.hpp"
#include "kernel/video/videocapture.h"
#include "kernel/video/videocontrol.h"
#include "kernel/video/videodecoderlz.h"
#include "kernel/video/videodecoderpcie.h"
#include "kernel/video/videoemuxer.h"
#include "kernel/video/videoformat.h"
#include "kernel/video/videoraw.h"
#include "kernel/video/videostream.h"
#include "kernel/video/videov4lv_key.h"
#include "kernel/video/videov4lvstate.h"
#include "kernel/video/videovirtual.h"
#include "kernel/video/videoyuv.h"
namespace hype::video {
    namespace {
        hcpp::u8 rgb_out[vcap_width * vcap_height * 4];
        const hcpp::u8 expected_rgb[vcap_bars][3] = {{255, 255, 255}, {255, 255, 0},   {0, 255, 255},
                                                   {0, 255, 0},     {255, 0, 255},   {255, 0, 0},
                                                   {0, 0, 255},     {0, 0, 0}};
        bool close_enough(hcpp::u8 a, hcpp::u8 b) noexcept {
            const int d = int(a) - int(b);
            return d > -6 && d < 6;
        }
        hcpp::u32 decode_counter(const VideoFrame& f) noexcept {
            hcpp::u32 v = 0;
            for (hcpp::u32 bit = 0; bit < 8; ++bit)
                if (f.data[4 * f.width + bit * 8 + 4] > 127) v |= 1u << bit;
            return v;
        }
        bool verify_bars(const VideoFrame& f) noexcept {
            const hcpp::u32 y = f.height / 3;
            for (hcpp::u32 bar = 0; bar < vcap_bars; ++bar) {
                const hcpp::u32 x = bar * (f.width / vcap_bars) + f.width / (vcap_bars * 2);
                const hcpp::u8* p = f.data + (y * f.width + x) * 4;
                if (!close_enough(p[0], expected_rgb[bar][0]) ||
                    !close_enough(p[1], expected_rgb[bar][1]) ||
                    !close_enough(p[2], expected_rgb[bar][2]))
                    return false;
            }
            return true;
        }
    }  // namespace
        hcpp::u8 lzf_src[16 * 8 * 4];
        hcpp::u8 lzf_dst[16 * 8 * 4];
        hcpp::u8 lzf_enc[16 * 8 * 4 * 2 + 64];
        hcpp::u8 mjpeg_px[24 * 16 * 4];
        hcpp::u8 avi_buf[2048];
        hcpp::u8 raw_rgb[8 * 4 * 4];
        hcpp::u8 yuy2_src[4 * 2 * 2];

        void mark(const char* name, bool ok) noexcept {
            serial::write(name);
            serial::write_line(ok ? "ok" : "fail");
        }
        bool bytes_eq(const hcpp::u8* a, const hcpp::u8* b, hcpp::size_t n) noexcept {
            for (hcpp::size_t i = 0; i < n; ++i)
                if (a[i] != b[i]) return false;
            return true;
        }
        void wr32(hcpp::u8* p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v & 0xFF);
            p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
            p[2] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
            p[3] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
        }
        void tag(hcpp::u8* p, char a, char b, char c, char d) noexcept {
            p[0] = static_cast<hcpp::u8>(a);
            p[1] = static_cast<hcpp::u8>(b);
            p[2] = static_cast<hcpp::u8>(c);
            p[3] = static_cast<hcpp::u8>(d);
        }

        hcpp::size_t lzf_sample(hcpp::size_t& out_len) noexcept {
            for (hcpp::u32 y = 0; y < 8; ++y)
                for (hcpp::u32 x = 0; x < 16; ++x) {
                    hcpp::u8* p = lzf_src + (hcpp::size_t(y) * 16 + x) * 4;
                    p[0] = static_cast<hcpp::u8>((x / 4) * 40);
                    p[1] = static_cast<hcpp::u8>(x * 4);
                    p[2] = static_cast<hcpp::u8>(y * 16);
                    p[3] = 0xFF;
                }
            VideoFrame in{16, 8, 64, PixelFormat::RGB32, lzf_src, sizeof(lzf_src), 0};
            out_len = lzf_encode_frame(in, lzf_enc, sizeof(lzf_enc));
            return out_len;
        }
        bool lzf_roundtrip() noexcept {
            hcpp::size_t n = 0;
            if (lzf_sample(n) == 0 || !lzf_probe(lzf_enc, n)) return false;
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            const DecodeResult r = lz_decode_frame(lzf_enc, n, out);
            return r.ok && r.produced == sizeof(lzf_src) && out.width == 16 &&
                   out.height == 8 && out.format == PixelFormat::RGB32 &&
                   bytes_eq(lzf_src, lzf_dst, sizeof(lzf_src));
        }
        bool ffv1_stub_ok() noexcept {
            const hcpp::u8 tag4[] = {'F', 'F', 'V', '1'};
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            const DecodeResult r = ffv1_decode_frame(tag4, sizeof(tag4), out);
            return ffv1_probe(tag4, sizeof(tag4)) && !r.ok && r.codec == CodecType::Ffv1;
        }
        hcpp::size_t avi_build() noexcept {
            const hcpp::size_t jlen = sizeof(jpeg_selftest_blob);
            const hcpp::size_t pad = jlen & 1;
            hcpp::size_t p = 0;
            tag(avi_buf + p, 'R', 'I', 'F', 'F');
            wr32(avi_buf + p + 4, 0);
            tag(avi_buf + p + 8, 'A', 'V', 'I', ' ');
            p += 12;
            tag(avi_buf + p, 'L', 'I', 'S', 'T');
            wr32(avi_buf + p + 4, 4 + 8 + 56);
            tag(avi_buf + p + 8, 'h', 'd', 'r', 'l');
            p += 12;
            tag(avi_buf + p, 's', 't', 'r', 'h');
            wr32(avi_buf + p + 4, 56);
            tag(avi_buf + p + 8, 'v', 'i', 'd', 's');
            tag(avi_buf + p + 12, 'M', 'J', 'P', 'G');
            for (hcpp::u32 k = 16; k < 56; ++k) avi_buf[p + 8 + k] = 0;
            p += 8 + 56;
            tag(avi_buf + p, 'L', 'I', 'S', 'T');
            wr32(avi_buf + p + 4, static_cast<hcpp::u32>(4 + 8 + jlen + pad));
            tag(avi_buf + p + 8, 'm', 'o', 'v', 'i');
            p += 12;
            tag(avi_buf + p, '0', '0', 'd', 'c');
            wr32(avi_buf + p + 4, static_cast<hcpp::u32>(jlen));
            p += 8;
            for (hcpp::size_t k = 0; k < jlen; ++k) avi_buf[p + k] = jpeg_selftest_blob[k];
            p += jlen;
            if (pad) avi_buf[p++] = 0;
            wr32(avi_buf + 4, static_cast<hcpp::u32>(p - 8));
            return p;
        }
        bool mjpeg_ok() noexcept {
            VideoFrame f{0, 0, 0, PixelFormat::Unknown, mjpeg_px, sizeof(mjpeg_px), 0};
            DecodeResult r = mjpeg_decode_frame(jpeg_selftest_blob, sizeof(jpeg_selftest_blob), f);
            if (!r.ok || f.width != JPEG_SELFTEST_WIDTH || f.height != JPEG_SELFTEST_HEIGHT ||
                f.format != PixelFormat::RGB32)
                return false;
            const hcpp::size_t avi_len = avi_build();
            if (!emuxer_avi_probe(avi_buf, avi_len)) return false;
            const hcpp::u8* fr = nullptr;
            hcpp::size_t fl = 0;
            if (!emuxer_avi_mjpg_frame(avi_buf, avi_len, fr, fl)) return false;
            if (fl != sizeof(jpeg_selftest_blob) || !bytes_eq(fr, jpeg_selftest_blob, fl))
                return false;
            VideoFrame f2{0, 0, 0, PixelFormat::Unknown, mjpeg_px, sizeof(mjpeg_px), 0};
            r = mjpeg_decode_frame(fr, fl, f2);
            return r.ok && f2.width == JPEG_SELFTEST_WIDTH &&
                   f2.height == JPEG_SELFTEST_HEIGHT;
        }
        bool raw_ok() noexcept {
            for (hcpp::size_t i = 0; i < sizeof(raw_rgb); ++i) raw_rgb[i] = 0;
            const hcpp::u8 rgb_src[4 * 2 * 3] = {10, 20, 30, 40, 50, 60, 70, 80,
                                                 90, 100, 110, 120, 1, 2, 3, 4,
                                                 5, 6, 7, 8, 9, 10, 11, 12};
            VideoFrame rgb{0, 0, 0, PixelFormat::RGB24, raw_rgb, 4 * 2 * 3, 0};
            if (!raw_frame_unpack(rgb_src, sizeof(rgb_src), 4, 2, PixelFormat::RGB24, rgb))
                return false;
            if (rgb.width != 4 || rgb.height != 2 || rgb.size != 24 ||
                !bytes_eq(raw_rgb, rgb_src, 24))
                return false;
            for (hcpp::size_t i = 0; i < sizeof(raw_rgb); ++i) raw_rgb[i] = 0;
            yuy2_src[0] = 81;
            yuy2_src[1] = 90;
            yuy2_src[2] = 81;
            yuy2_src[3] = 240;
            for (hcpp::size_t i = 4; i < sizeof(yuy2_src); ++i)
                yuy2_src[i] = static_cast<hcpp::u8>(i);
            VideoFrame yuv{0, 0, 0, PixelFormat::RGB32, raw_rgb, sizeof(raw_rgb), 0};
            if (!raw_frame_unpack(yuy2_src, sizeof(yuy2_src), 4, 2, PixelFormat::YUY2, yuv))
                return false;
            if (yuv.width != 4 || yuv.height != 2 || yuv.format != PixelFormat::RGB32)
                return false;
            return raw_rgb[0] == 238 && raw_rgb[1] == 14 && raw_rgb[2] == 13 &&
                   raw_rgb[3] == 0xFF;
        }
        bool decodejob_ok() noexcept {
            hcpp::size_t n = 0;
            if (lzf_sample(n) == 0) return false;
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            DecodeJob j{CodecType::Lz, lzf_enc, n, &out, 0, DecodeJobStatus::Free, {}};
            if (!decodejob_submit(j) || j.status != DecodeJobStatus::Done || !j.result.ok)
                return false;
            if (!bytes_eq(lzf_src, lzf_dst, sizeof(lzf_src))) return false;
            DecodeJob hw{CodecType::Mjpeg, jpeg_selftest_blob, sizeof(jpeg_selftest_blob),
                         &out, decodejob_flag_hw, DecodeJobStatus::Free, {}};
            if (!decodejob_submit(hw) || hw.status != DecodeJobStatus::Unsupported) return false;
            return !pcie_decoder_available() && decodejob_pending() == 0;
        }
    bool video_core_selftest() noexcept {
        bool ok = true;
        const hcpp::i32 client = video_open_dev(0);
        bool cap = client >= 0 && video_req_bufs(client, 4) && !video_qbuf(client) && v4lv_stream_on(0) &&
                   !v4lv_stream_on(0);
        VideoFrame frames[3]{};
        hcpp::u64 base = 0;
        for (hcpp::u32 i = 0; i < 3 && cap; ++i) {
            cap = video_qbuf(client) && video_dqbuf(client, frames[i]) && frame_valid(frames[i]);
            if (cap && i == 0) base = frames[i].timestamp;
            cap = cap && frames[i].timestamp == base + i && decode_counter(frames[i]) == (base + i);
        }
        serial::write("vcap frame=");
        serial::write_line(cap ? "ok" : "fail");
        ok = ok && cap;
        VideoFrame rgb{vcap_width, vcap_height, vcap_width * 4, PixelFormat::RGB32, rgb_out, sizeof(rgb_out), 0};
        const bool conv = nv12_to_rgb32(frames[0], rgb) && frame_valid(rgb) && verify_bars(rgb);
        serial::write("yuv conv=");
        serial::write_line(conv ? "ok" : "fail");
        ok = ok && conv;
        VideoFrame scratch{};
        bool strm = v4lv_stream_off(0) && v4lv_state(0) == VideoDeviceState::Open &&
                    !video_dqbuf(client, scratch) && !video_qbuf(client) && v4lv_stream_on(0);
        for (hcpp::u32 i = 0; i < video_stream_max_buffers + 1 && strm; ++i) strm = video_qbuf(client);
        const VideoStream* s = v4lv_stream(0);
        strm = strm && s != nullptr && stream_pending(*s) == video_stream_max_buffers && s->dropped != 0;
        strm = strm && stream_pop(*v4lv_stream(0), scratch) && scratch.timestamp == base + 3 + 1;
        while (stream_pop(*v4lv_stream(0), scratch)) {
        }
        strm = strm && v4lv_stream_off(0) && v4lv_state(0) == VideoDeviceState::Open;
        serial::write("v4lv stream=");
        serial::write_line(strm ? "ok" : "fail");
        ok = ok && strm;
        hcpp::i32 v = 0;
        const bool ctrl = control_set(0, VideoControlId::Brightness, 999) &&
                          control_get(0, VideoControlId::Brightness, v) && v == 127 &&
                          control_set(0, VideoControlId::Brightness, -999) &&
                          control_get(0, VideoControlId::Brightness, v) && v == -128 &&
                          control_get(0, VideoControlId::Saturation, v) && v == 128;
        serial::write("video ctrl=");
        serial::write_line(ctrl ? "ok" : "fail");
        ok = ok && ctrl;
        VideoDeviceInfo info{};
        const bool fmt = format_negotiate(info, 640, 480, PixelFormat::RGB24) &&
                         format_find(PixelFormat::NV12) != nullptr &&
                         format_find(PixelFormat::NV12)->fourcc == v4lv_key_nv12;
        serial::write("video fmt=");
        serial::write_line(fmt ? "ok" : "fail");
        ok = ok && fmt;
        serial::write("video core=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }
    bool video_decode_selftest() noexcept {
        bool ok = true;
        bool good = lzf_roundtrip();
        mark("lzf rt=", good);
        ok = ok && good;
        good = ffv1_stub_ok();
        serial::write("ffv1=");
        serial::write_line(good ? "stub" : "fail");
        ok = ok && good;
        good = mjpeg_ok();
        mark("mjpeg=", good);
        ok = ok && good;
        good = raw_ok();
        mark("raw frame=", good);
        ok = ok && good;
        good = decodejob_ok();
        mark("decodejob=", good);
        ok = ok && good;
        return ok;
    }
}  // namespace hype::video
