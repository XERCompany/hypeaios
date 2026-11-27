#include "kernel/video/videovirtual.h"

#include "kernel/video/videocontrol.h"
#include "kernel/video/videov4lvstate.h"
#include "kernel/video/videoyuv.h"
namespace hype::video {
    namespace {
        constexpr hcpp::size_t pool_depth = 4;
        constexpr hcpp::size_t frame_bytes = vcap_width * vcap_height * 3 / 2;
        constexpr hcpp::u32 box_w = 24;
        constexpr hcpp::u32 box_h = 12;
        hcpp::u8 pool[pool_depth][frame_bytes];
        hcpp::u32 sequence;
        const hcpp::u8 bar_rgb[vcap_bars][3] = {{255, 255, 255}, {255, 255, 0},   {0, 255, 255},
                                                {0, 255, 0},     {255, 0, 255},   {255, 0, 0},
                                                {0, 0, 255},     {0, 0, 0}};
        void neutral_uv(hcpp::u8* buf, hcpp::u32 x0, hcpp::u32 y0, hcpp::u32 w, hcpp::u32 h) noexcept {
            hcpp::u8* uv = buf + vcap_width * vcap_height;
            for (hcpp::u32 cy = y0 / 2; cy <= (y0 + h - 1) / 2; ++cy)
                for (hcpp::u32 cx = x0 / 2; cx <= (x0 + w - 1) / 2; ++cx) {
                    uv[cy * vcap_width + cx * 2] = 128;
                    uv[cy * vcap_width + cx * 2 + 1] = 128;
                }
        }
        void render(hcpp::u8* buf, hcpp::u32 seq, hcpp::i32 brightness) noexcept {
            hcpp::u8 ys[vcap_bars], us[vcap_bars], vs[vcap_bars];
            for (hcpp::u32 i = 0; i < vcap_bars; ++i)
                rgb_to_yuv(bar_rgb[i][0], bar_rgb[i][1], bar_rgb[i][2], ys[i], us[i], vs[i]);
            const hcpp::u32 bar_w = vcap_width / vcap_bars;
            for (hcpp::u32 y = 0; y < vcap_height; ++y)
                for (hcpp::u32 x = 0; x < vcap_width; ++x)
                    buf[y * vcap_width + x] = yuv_clamp(ys[x / bar_w] + brightness);
            hcpp::u8* uv = buf + vcap_width * vcap_height;
            for (hcpp::u32 cy = 0; cy < vcap_height / 2; ++cy)
                for (hcpp::u32 cx = 0; cx < vcap_width / 2; ++cx) {
                    const hcpp::u32 bar = cx * 2 / bar_w;
                    uv[cy * vcap_width + cx * 2] = us[bar];
                    uv[cy * vcap_width + cx * 2 + 1] = vs[bar];
                }
            const hcpp::u32 bx = (seq * 7) % (vcap_width - box_w);
            const hcpp::u32 by = vcap_height - box_h - 8;
            for (hcpp::u32 y = by; y < by + box_h; ++y)
                for (hcpp::u32 x = bx; x < bx + box_w; ++x) {
                    const bool edge = y == by || y == by + box_h - 1 || x == bx || x == bx + box_w - 1;
                    buf[y * vcap_width + x] = yuv_clamp((edge ? 235 : 16) + brightness);
                }
            neutral_uv(buf, bx, by, box_w, box_h);
            for (hcpp::u32 bit = 0; bit < 8; ++bit) {
                const hcpp::u8 cell = ((seq >> bit) & 1) ? 235 : 16;
                for (hcpp::u32 y = 0; y < 8; ++y)
                    for (hcpp::u32 x = bit * 8; x < bit * 8 + 8; ++x)
                        buf[y * vcap_width + x] = cell;
                neutral_uv(buf, bit * 8, 0, 8, 8);
            }
        }
    }  // namespace
    bool virtual_video_register() noexcept {
        return v4lv_register_device(
            {0,
             static_cast<hcpp::u32>(VideoDeviceCapability::Capture) |
                 static_cast<hcpp::u32>(VideoDeviceCapability::Streaming) |
                 static_cast<hcpp::u32>(VideoDeviceCapability::Virtual),
             vcap_width, vcap_height, PixelFormat::NV12});
    }
    bool vcap_open(hcpp::u32 id) noexcept {
        if (v4lv_device_by_id(id) == nullptr) return false;
        const VideoDeviceState state = v4lv_state(id);
        if (state == VideoDeviceState::Open) return true;
        return state == VideoDeviceState::Idle && v4lv_set_state(id, VideoDeviceState::Open);
    }
    bool vcap_frame(hcpp::u32 id, VideoFrame& out) noexcept {
        const VideoDeviceInfo* d = v4lv_device_by_id(id);
        if (d == nullptr || !v4lv_can_capture(id) || d->width != vcap_width || d->height != vcap_height ||
            d->format != PixelFormat::NV12)
            return false;
        hcpp::i32 brightness = 0;
        control_get(id, VideoControlId::Brightness, brightness);
        hcpp::u8* buf = pool[sequence % pool_depth];
        render(buf, sequence, brightness);
        out.width = vcap_width;
        out.height = vcap_height;
        out.pitch = vcap_width;
        out.format = PixelFormat::NV12;
        out.data = buf;
        out.size = frame_bytes;
        out.timestamp = sequence;
        ++sequence;
        return true;
    }
    hcpp::u32 vcap_frame_index() noexcept { return sequence; }
}  // namespace hype::video
