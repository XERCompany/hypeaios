#include "kernel/video/videocoretest.h"

#include "kernel/serial.hpp"
#include "kernel/video/videoclient.h"
#include "kernel/video/videocontrol.h"
#include "kernel/video/videoformat.h"
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
}  // namespace hype::video
