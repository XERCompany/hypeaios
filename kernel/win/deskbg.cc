#include "kernel/win/deskbg.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/image/imageutil.h"
#include "kernel/display/scaler.h"
#include "kernel/memory/pmm.h"

namespace hype::win {

    namespace {

        constexpr hcpp::u32 wall_bytes =
            desk_bg_wall_width * desk_bg_wall_height * 4;
        constexpr hcpp::u32 src_max_dim = 4096;

        DeskBgMode bg_mode = DeskBgMode::Gradient;
        hype::codec::Arena bg_arena{};
        hcpp::size_t bg_arena_pages = 0;
        hcpp::u32* wall_px = nullptr;

        hcpp::u32 cover_scale_q16(hcpp::u32 src_w, hcpp::u32 src_h, hcpp::u32 dst_w,
                                  hcpp::u32 dst_h) noexcept {
            const hcpp::u32 sx = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(dst_w) << 16) / src_w);
            const hcpp::u32 sy = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(dst_h) << 16) / src_h);
            return sx > sy ? sx : sy;
        }

        bool ensure_arena(hcpp::size_t needed) noexcept {
            if (bg_arena.base != nullptr && bg_arena.cap >= needed) {
                return true;
            }
            if (bg_arena.base != nullptr) {
                hype::codec::arena_release_pmm(bg_arena, bg_arena_pages);
                bg_arena = {};
                bg_arena_pages = 0;
                wall_px = nullptr;
            }
            const hcpp::size_t pages =
                (needed + hype::memory::page_size - 1) / hype::memory::page_size;
            if (!hype::codec::arena_from_pmm(bg_arena, pages)) {
                return false;
            }
            bg_arena_pages = pages;
            return true;
        }

        void frame_to_packed(const hype::codec::FrameBuf& frame, hcpp::u32* dst,
                             hcpp::u32 stride) noexcept {
            const hcpp::u32 bpp =
                frame.format == video::PixelFormat::RGB32 ? 4 : 3;
            const hcpp::u32 pitch = hype::codec::image::source_pitch(frame);
            for (hcpp::u32 y = 0; y < frame.height; ++y) {
                const hcpp::u8* src_row =
                    frame.data + static_cast<hcpp::size_t>(y) * pitch;
                hcpp::u32* dst_row = dst + static_cast<hcpp::size_t>(y) * stride;
                for (hcpp::u32 x = 0; x < frame.width; ++x) {
                    const hcpp::u8* p = src_row + static_cast<hcpp::size_t>(x) * bpp;
                    const hcpp::u32 a = bpp == 4 ? p[3] : 0xFF;
                    dst_row[x] = (a << 24) | (static_cast<hcpp::u32>(p[0]) << 16) |
                                 (static_cast<hcpp::u32>(p[1]) << 8) | p[2];
                }
            }
        }

    }

    bool desk_bg_set_image(const hype::codec::FrameBuf& frame) noexcept {
        if (!hype::codec::image::frame_source_valid(&frame) ||
            frame.width > src_max_dim || frame.height > src_max_dim) {
            return false;
        }
        const hcpp::size_t src_bytes =
            static_cast<hcpp::size_t>(frame.width) * frame.height * 4;
        if (!ensure_arena(static_cast<hcpp::size_t>(wall_bytes) + src_bytes)) {
            return false;
        }
        wall_px = reinterpret_cast<hcpp::u32*>(bg_arena.base);
        hcpp::u32* src = reinterpret_cast<hcpp::u32*>(bg_arena.base + wall_bytes);
        frame_to_packed(frame, src, frame.width);
        const hcpp::u32 scale = cover_scale_q16(frame.width, frame.height,
                                              desk_bg_wall_width,
                                              desk_bg_wall_height);
        hcpp::u32 eff_w = static_cast<hcpp::u32>(
            (static_cast<hcpp::u64>(desk_bg_wall_width) << 16) / scale);
        hcpp::u32 eff_h = static_cast<hcpp::u32>(
            (static_cast<hcpp::u64>(desk_bg_wall_height) << 16) / scale);
        if (eff_w == 0) eff_w = 1;
        if (eff_h == 0) eff_h = 1;
        if (eff_w > frame.width) eff_w = frame.width;
        if (eff_h > frame.height) eff_h = frame.height;
        const hype::display::ScaleRect src_rect{
            (frame.width - eff_w) / 2, (frame.height - eff_h) / 2, eff_w, eff_h};
        const hype::display::ScaleRect dst_rect{0, 0, desk_bg_wall_width,
                                                desk_bg_wall_height};
        if (!hype::display::scale_bilinear(src, frame.width, frame.height,
                                           frame.width, src_rect, wall_px,
                                           desk_bg_wall_width, desk_bg_wall_height,
                                           desk_bg_wall_width, dst_rect)) {
            return false;
        }
        bg_mode = DeskBgMode::Image;
        return true;
    }

    void desk_bg_reset() noexcept { bg_mode = DeskBgMode::Gradient; }

    DeskBgMode desk_bg_mode() noexcept { return bg_mode; }

    bool desk_bg_ready() noexcept {
        return bg_mode == DeskBgMode::Image && wall_px != nullptr;
    }

    hcpp::u32 desk_bg_sample(hcpp::u32 x, hcpp::u32 y) noexcept {
        if (wall_px == nullptr || x >= desk_bg_wall_width ||
            y >= desk_bg_wall_height) {
            return 0;
        }
        return wall_px[static_cast<hcpp::size_t>(y) * desk_bg_wall_width + x];
    }

    bool desk_bg_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid() || !desk_bg_ready()) {
            return false;
        }
        const hcpp::u32 fw = fb.width();
        const hcpp::u32 fh = fb.height();
        if (fw == 0 || fh == 0) {
            return false;
        }
        const hcpp::u32 scale =
            cover_scale_q16(desk_bg_wall_width, desk_bg_wall_height, fw, fh);
        hcpp::u32 eff_w =
            static_cast<hcpp::u32>((static_cast<hcpp::u64>(fw) << 16) / scale);
        hcpp::u32 eff_h =
            static_cast<hcpp::u32>((static_cast<hcpp::u64>(fh) << 16) / scale);
        if (eff_w == 0) eff_w = 1;
        if (eff_h == 0) eff_h = 1;
        if (eff_w > desk_bg_wall_width) eff_w = desk_bg_wall_width;
        if (eff_h > desk_bg_wall_height) eff_h = desk_bg_wall_height;
        const hcpp::u32 off_x = (desk_bg_wall_width - eff_w) / 2;
        const hcpp::u32 off_y = (desk_bg_wall_height - eff_h) / 2;
        for (hcpp::u32 y = 0; y < fh; ++y) {
            const hcpp::u32 sy = off_y + (y * eff_h) / fh;
            const hcpp::u32* row =
                wall_px + static_cast<hcpp::size_t>(sy) * desk_bg_wall_width;
            for (hcpp::u32 x = 0; x < fw; ++x) {
                const hcpp::u32 sx = off_x + (x * eff_w) / fw;
                const hcpp::u32 px = row[sx];
                fb.put_pixel(x, y,
                             fb.encode_rgb(static_cast<hcpp::u8>(px >> 16),
                                           static_cast<hcpp::u8>(px >> 8),
                                           static_cast<hcpp::u8>(px)));
            }
        }
        return true;
    }

}
