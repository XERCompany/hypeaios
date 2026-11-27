#include "kernel/gdxf/gdxf.h"

#include "hcpp/graphics/text.hpp"
#include "hcpp/text_writer.hpp"
#include "kernel/gdxf/gdxfint.h"
#include "kernel/memory/pmm.h"
#include "kernel/win/winman.h"

namespace hype::gdxf {

    namespace {

        void blt_rect(const hype::ui::Rect& rect) noexcept {
            const hype::ui::Rect surface_bounds{
                0, 0, static_cast<hcpp::i32>(g_gdxf.surf_w),
                static_cast<hcpp::i32>(g_gdxf.surf_h)};
            const hype::ui::Rect clipped = rect.intersection(surface_bounds);
            if (clipped.empty()) {
                return;
            }
            const hype::ui::Rect dst{
                g_gdxf.present_dst.x + clipped.x,
                g_gdxf.present_dst.y + clipped.y, clipped.w, clipped.h};
            const hype::ui::Rect screen{
                0, 0, static_cast<hcpp::i32>(g_gdxf.scanout.width()),
                static_cast<hcpp::i32>(g_gdxf.scanout.height())};
            const hype::ui::Rect on = dst.intersection(screen);
            if (on.empty()) {
                return;
            }
            for (hcpp::i32 y = 0; y < on.h; ++y) {
                const hcpp::i32 sy = on.y - g_gdxf.present_dst.y + y;
                const auto* src = reinterpret_cast<const hcpp::u32*>(
                    g_gdxf.backbuf +
                    static_cast<hcpp::size_t>(sy) * g_gdxf.surf_w * 4) +
                                  (on.x - g_gdxf.present_dst.x);
                const hcpp::i32 dy = on.y + y;
                for (hcpp::i32 x = 0; x < on.w; ++x) {
                    const hcpp::u32 px = src[x];
                    g_gdxf.scanout.put_pixel(
                        static_cast<hcpp::u32>(on.x + x),
                        static_cast<hcpp::u32>(dy),
                        g_gdxf.scanout.encode_rgb(
                            static_cast<hcpp::u8>(px >> 16),
                            static_cast<hcpp::u8>(px >> 8),
                            static_cast<hcpp::u8>(px)));
                }
            }
            win::wm_invalidate(on);  // hand the dirty rect to the compositor
        }

        void draw_fps_overlay() noexcept {
            if (g_gdxf.surf_h < 20 || g_gdxf.backbuf == nullptr) {
                return;
            }
            hcpp::graphics::Framebuffer target(
                g_gdxf.backbuf, g_gdxf.surf_w, g_gdxf.surf_h, g_gdxf.surf_w * 4,
                32, 8, 16, 8, 8, 8, 0);
            hcpp::TextWriter text;
            text.append("GDXF ");
            const hcpp::u32 us =
                g_gdxf.frame_us_avg != 0 ? g_gdxf.frame_us_avg : 1;
            text.append_dec32(1000000u / us);
            text.append("FPS");
            hcpp::graphics::draw_text(target, 4, 4, text.view(), 0xFFCC00, 1);
        }

    }  // namespace

    bool gdxf_surface_resize(hcpp::u32 w, hcpp::u32 h) noexcept {
        if (w == 0 || h == 0) {
            return false;
        }
        if (g_gdxf.backbuf != nullptr && g_gdxf.surf_w == w &&
            g_gdxf.surf_h == h) {
            return true;
        }
        const hcpp::size_t pages =
            (static_cast<hcpp::size_t>(w) * h * 4 + memory::page_size - 1) /
            memory::page_size;
        const hcpp::uintptr_t addr = memory::pmm_allocate_pages(pages);
        if (addr == 0) {
            return false;
        }
        if (g_gdxf.backbuf != nullptr) {
            memory::pmm_free_pages(
                reinterpret_cast<hcpp::uintptr_t>(g_gdxf.backbuf),
                g_gdxf.backbuf_pages);
        }
        g_gdxf.backbuf = reinterpret_cast<hcpp::u8*>(addr);
        g_gdxf.surf_w = w;
        g_gdxf.surf_h = h;
        g_gdxf.backbuf_pages = pages;
        g_gdxf.surf_acquired = false;
        g_gdxf.dirty_count = 0;
        return true;
    }

    bool gdxf_surface_acquire(Surface& surf) noexcept {
        if (!g_gdxf.initialized || g_gdxf.backbuf == nullptr ||
            g_gdxf.surf_acquired) {
            return false;
        }
        surf.pixels = g_gdxf.backbuf;
        surf.width = g_gdxf.surf_w;
        surf.height = g_gdxf.surf_h;
        surf.pitch_bytes = g_gdxf.surf_w * 4;
        surf.format = PixelFormat::XRGB32;
        g_gdxf.surf_acquired = true;
        return true;
    }

    void gdxf_present(const hype::ui::Rect* dirty, hcpp::u32 n) noexcept {
        if (!g_gdxf.initialized || g_gdxf.backbuf == nullptr ||
            !g_gdxf.scanout.valid()) {
            g_gdxf.dirty_count = 0;
            g_gdxf.surf_acquired = false;
            return;
        }
        if (g_gdxf.fps_overlay) {
            draw_fps_overlay();
            gdxf_dirty_add(hype::ui::Rect{4, 4, 96, 12});
        }
        if (dirty != nullptr && n != 0) {
            for (hcpp::u32 i = 0; i < n; ++i) {
                blt_rect(dirty[i]);
            }
            // internal dirty (fps overlay, sprite batch) still applies
            for (hcpp::u32 i = 0; i < g_gdxf.dirty_count; ++i) {
                blt_rect(g_gdxf.dirty[i]);
            }
        } else if (g_gdxf.dirty_count != 0) {
            for (hcpp::u32 i = 0; i < g_gdxf.dirty_count; ++i) {
                blt_rect(g_gdxf.dirty[i]);
            }
        } else {
            blt_rect(hype::ui::Rect{0, 0,
                                    static_cast<hcpp::i32>(g_gdxf.surf_w),
                                    static_cast<hcpp::i32>(g_gdxf.surf_h)});
        }
        g_gdxf.dirty_count = 0;
        g_gdxf.batch_open = false;
        g_gdxf.surf_acquired = false;
        g_gdxf.presented = true;
    }

    hcpp::u32 gdxf_surface_count() noexcept { return 1; }

    bool gdxf_fullscreen_direct() noexcept {
        return g_gdxf.initialized && !g_gdxf.windowed;
    }

    bool gdxf_bind_window(hype::ui::Rect dst) noexcept {
        if (!g_gdxf.initialized || dst.empty()) {
            return false;
        }
        if (!gdxf_surface_resize(static_cast<hcpp::u32>(dst.w),
                                 static_cast<hcpp::u32>(dst.h))) {
            return false;
        }
        g_gdxf.windowed = true;
        g_gdxf.present_dst = dst;
        return true;
    }

    void gdxf_unbind_window() noexcept {
        g_gdxf.windowed = false;
        g_gdxf.present_dst = hype::ui::Rect{
            0, 0, static_cast<hcpp::i32>(g_gdxf.scanout.width()),
            static_cast<hcpp::i32>(g_gdxf.scanout.height())};
        gdxf_surface_resize(g_gdxf.scanout.width(), g_gdxf.scanout.height());
    }

    void gdxf_dirty_add(hype::ui::Rect rect) noexcept {
        const hype::ui::Rect bounds{
            0, 0, static_cast<hcpp::i32>(g_gdxf.surf_w),
            static_cast<hcpp::i32>(g_gdxf.surf_h)};
        const hype::ui::Rect clipped = rect.intersection(bounds);
        if (clipped.empty()) {
            return;
        }
        for (hcpp::u32 i = 0; i < g_gdxf.dirty_count; ++i) {
            if (g_gdxf.dirty[i].intersects(clipped)) {
                g_gdxf.dirty[i] = g_gdxf.dirty[i].united(clipped);
                return;
            }
        }
        if (g_gdxf.dirty_count < gdxf_max_dirty_rects) {
            g_gdxf.dirty[g_gdxf.dirty_count++] = clipped;
            return;
        }
        hype::ui::Rect all = clipped;
        for (hcpp::u32 i = 0; i < g_gdxf.dirty_count; ++i) {
            all = all.united(g_gdxf.dirty[i]);
        }
        g_gdxf.dirty[0] = all;
        g_gdxf.dirty_count = 1;
    }

}  // namespace hype::gdxf
