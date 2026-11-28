#include "kernel/win/deskman.h"

#include "hcpp/memory.hpp"
#include "kernel/gfx/hypegfx.h"
#include "kernel/xdp/xdp.h"
#include "kernel/hxrt/apps/speedstart.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/win/deskbg.h"
#include "kernel/win/deskicons.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/win/winman.h"

namespace hype::win {

    namespace {

        bool desk_built = false;

        // Render-audit tally: defects found and fixed in the paint pipeline
        // (see devin/p18-render notes): unclipped styled text, clip re-expand
        // in stroke_rounded_rect, title/task/clock glyph bleed, alpha blend
        // rounding drift, missing shadow-spill damage on frames / hover /
        // switcher / toasts / QS / start menu, missed damage on focus-raise,
        // 256 px shadow-mask truncation, and the missing compositor buffer.
        constexpr hcpp::u32 render_audit_fixes = 13;

        // Persistent compositor back buffer: every shell layer paints into
        // this PMM-resident surface in the scanout pixel format, then only
        // the damaged clip rect is memcpy'd to the LFB. Painting straight
        // into scanout made every alpha/shadow blend read uncached video
        // memory and exposed half-finished repaints; composing offscreen
        // kills both the flicker and the read-modify-write stall.
        hcpp::graphics::Framebuffer back_fb;
        hcpp::uintptr_t back_base = 0;
        hcpp::size_t back_pages = 0;
        bool render_announced = false;

        bool ensure_back_buffer(
            const hcpp::graphics::Framebuffer& fb) noexcept {
            if (back_fb.valid() && back_fb.width() == fb.width() &&
                back_fb.height() == fb.height()) {
                return true;
            }
            const hcpp::u32 bpp = fb.bytes_per_pixel();
            if (!fb.valid() || (bpp != 3 && bpp != 4)) {
                return false;
            }
            const hcpp::size_t bytes =
                static_cast<hcpp::size_t>(fb.width()) * fb.height() * bpp;
            const hcpp::size_t pages =
                (bytes + hype::memory::page_size - 1) /
                hype::memory::page_size;
            if (pages == 0) {
                return false;
            }
            const hcpp::uintptr_t mem =
                hype::memory::pmm_allocate_pages(pages);
            if (mem == 0) {
                return false;
            }
            if (back_base != 0) {
                hype::memory::pmm_free_pages(back_base, back_pages);
            }
            back_base = mem;
            back_pages = pages;
            back_fb = fb.with_base(reinterpret_cast<volatile hcpp::u8*>(mem));
            return back_fb.valid();
        }

        // Flush the damaged clip from the back buffer to scanout. Rows are
        // copied as raw bytes — the back buffer shares the scanout pixel
        // format, so no conversion is needed.
        void blit_clip(hcpp::graphics::Framebuffer& fb,
                       const hype::ui::Rect& clip) noexcept {
            const hcpp::u32 bpp = fb.bytes_per_pixel();
            const hcpp::size_t row_bytes =
                static_cast<hcpp::size_t>(clip.w) * bpp;
            volatile hcpp::u8* dst =
                fb.base() + static_cast<hcpp::size_t>(clip.y) * fb.pitch() +
                static_cast<hcpp::size_t>(clip.x) * bpp;
            const hcpp::u8* src = const_cast<const hcpp::u8*>(
                back_fb.base() +
                static_cast<hcpp::size_t>(clip.y) * back_fb.pitch() +
                static_cast<hcpp::size_t>(clip.x) * bpp);
            for (hcpp::i32 y = 0; y < clip.h; ++y) {
                hcpp::copy(const_cast<hcpp::u8*>(dst), src, row_bytes);
                dst += fb.pitch();
                src += back_fb.pitch();
            }
        }

    }

    hcpp::u32 desk_open_app(const char* name) noexcept {
        const hcpp::u32 id = wm_open(name, hype::ui::Rect{});
        if (id != 0) {
            hype::ui::deskbar_notify("Launched", name);
        }
        return id;
    }

    hcpp::u32 desk_window_count() noexcept { return wm_count(); }

    hcpp::u32 desk_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return 0;
        }
        const hcpp::u64 paint_t0 = hype::time::uptime_ns();
        const bool first = !desk_built;
        if (first) {
            desk_built = true;
            const hcpp::i32 fw = static_cast<hcpp::i32>(fb.width());
            const hcpp::i32 fh = static_cast<hcpp::i32>(fb.height());
            wm_open("SpeedStart",
                    hype::ui::Rect{(fw - 520) / 2, (fh - 420) / 2, 520, 420});
            wm_invalidate_all();
        }
        // XDP render manager (issue #108): when the core allocated a
        // compositor surface it serves as the back buffer — paint into
        // it and xdp_present() fans the damaged rect out to every
        // present-capable backend (software -> VBE scanout, virtio-gpu
        // -> host resource). Without XDP the persistent PMM back buffer
        // (issue #110) fills the same role; without either, paint goes
        // straight into the LFB.
        // Static so wm_paint's stored `w.ctx.fb` pointer never
        // references a dead stack slot.
        static hcpp::graphics::Framebuffer xdp_view;
        hype::xdp::xdp_surface* xsurf = hype::xdp::xdp_compositor_surface();
        const bool have_bb = xsurf == nullptr && ensure_back_buffer(fb);
        if (xsurf != nullptr) {
            xdp_view = hype::xdp::xdp_surface_view(*xsurf);
        }
        hcpp::graphics::Framebuffer& target =
            xsurf != nullptr ? xdp_view : (have_bb ? back_fb : fb);
        // Banded z-order contract (docs/research/desktop-shell.md §7):
        // wallpaper → desk icons → wm_paint (windows ascending z, snap
        // preview, switcher, modal overlay) → taskbar band → cursor sprite
        // (drawn into scanout by the main loop after this returns).
        const hype::ui::Rect screen{
            0, 0, static_cast<hcpp::i32>(fb.width()),
            static_cast<hcpp::i32>(fb.height())};
        const hype::ui::Rect clip = wm_damage_clip(screen);
        if (!clip.empty()) {
            hype::ui::Canvas canvas(target);
            const hype::ui::Theme& theme = hype::ui::theme_active();
            canvas.set_clip(clip);
            if (!desk_bg_paint_clip(target, clip)) {
                hype::gfx::gfx_gradient_v(canvas, screen, theme.bg,
                                          hype::ui::rui_shade(theme.bg, 10));
            }
            // Desktop icon band: above wallpaper, below windows. Painted
            // only inside the damage clip so untouched cells stay cheap.
            desk_icons_paint(target, clip);
        }
        wm_paint(target);
        hype::ui::deskbar_paint(target, clip);
        if (xsurf != nullptr) {
            // wm_paint() repaints the FULL screen (not just the damage
            // clip) while a menu/context overlay is open — present the
            // same region it painted so no overlay pixels get stranded
            // in the surface.
            hype::ui::Rect dirty = clip;
            if (wm_menu_visible() || wm_ctx_visible() ||
                (dirty.empty() && first)) {
                dirty = screen;
            }
            hype::xdp::xdp_present(*xsurf, &dirty,
                                   dirty.empty() ? 0 : 1);
        } else if (have_bb && !clip.empty()) {
            blit_clip(fb, clip);
        }
        if (!render_announced && !clip.empty()) {
            render_announced = true;
            serial::write("render audit=");
            serial::write_dec32(render_audit_fixes);
            serial::write("\r\n");
            serial::write_line((have_bb || xsurf != nullptr)
                                   ? "render bb=ok"
                                   : "render bb=fail");
            const hcpp::u64 paint_us =
                (hype::time::uptime_ns() - paint_t0) / 1000;
            serial::write("render paint us=");
            serial::write_dec32(static_cast<hcpp::u32>(paint_us));
            serial::write("\r\n");
        }
        if (first) {
            hype::hxrt::ss_selftest();
        }
        return wm_count();
    }

}
