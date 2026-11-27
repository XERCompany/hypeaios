#include "kernel/win/deskman.h"

#include "kernel/gfx/hypegfx.h"
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
            hype::ui::Canvas canvas(fb);
            const hype::ui::Theme& theme = hype::ui::theme_active();
            canvas.set_clip(clip);
            if (!desk_bg_paint_clip(fb, clip)) {
                hype::gfx::gfx_gradient_v(canvas, screen, theme.bg,
                                          hype::ui::rui_shade(theme.bg, 10));
            }
            // Desktop icon band: above wallpaper, below windows. Painted
            // only inside the damage clip so untouched cells stay cheap.
            desk_icons_paint(fb, clip);
        }
        wm_paint(fb);
        hype::ui::deskbar_paint(fb, clip);
        if (first) {
            hype::hxrt::ss_selftest();
        }
        return wm_count();
    }

}
