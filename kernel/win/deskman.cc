#include "kernel/win/deskman.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/xdp/xdp.h"
#include "kernel/hxrt/apps/speedstart.h"
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
        // XDP render manager (issue #108): when the core allocated a
        // compositor surface, the whole desktop is painted into that
        // PMM-backed BGRA32 buffer instead of the LFB, then flipped via
        // xdp_present() which fans the damaged rect out to every
        // present-capable backend (software -> VBE scanout, virtio-gpu
        // -> host resource). The surface persists across frames so
        // damage-tracked partial repaints keep a complete image.
        // Without XDP the original paint-straight-into-LFB path runs.
        // Static so wm_paint's stored `w.ctx.fb` pointer (refreshed on
        // every repaint, but kept in the persistent Win struct) never
        // references a dead stack slot.
        static hcpp::graphics::Framebuffer xdp_view;
        hcpp::graphics::Framebuffer* target = &fb;
        hype::xdp::xdp_surface* xsurf = hype::xdp::xdp_compositor_surface();
        if (xsurf != nullptr) {
            xdp_view = hype::xdp::xdp_surface_view(*xsurf);
            target = &xdp_view;
        }
        hcpp::graphics::Framebuffer& out = *target;
        const bool first = !desk_built;
        if (first) {
            desk_built = true;
            const hcpp::i32 fw = static_cast<hcpp::i32>(fb.width());
            const hcpp::i32 fh = static_cast<hcpp::i32>(fb.height());
            wm_open("SpeedStart",
                    hype::ui::Rect{(fw - 520) / 2, (fh - 420) / 2, 520, 420});
            wm_invalidate_all();
        }
        const hype::ui::Rect screen{
            0, 0, static_cast<hcpp::i32>(fb.width()),
            static_cast<hcpp::i32>(fb.height())};
        const hype::ui::Rect clip = wm_damage_clip(screen);
        if (!clip.empty()) {
            hype::ui::Canvas canvas(out);
            const hype::ui::Theme& theme = hype::ui::theme_active();
            canvas.set_clip(clip);
            if (!desk_bg_paint_clip(out, clip)) {
                hype::gfx::gfx_gradient_v(canvas, screen, theme.bg,
                                          hype::ui::rui_shade(theme.bg, 10));
            }
            // Desktop icon band: above wallpaper, below windows. Painted
            // only inside the damage clip so untouched cells stay cheap.
            desk_icons_paint(out, clip);
        }
        wm_paint(out);
        hype::ui::deskbar_paint(out, clip);
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
        }
        if (first) {
            hype::hxrt::ss_selftest();
        }
        return wm_count();
    }

}
