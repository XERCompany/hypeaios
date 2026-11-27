#include "kernel/win/deskman.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/win/deskbg.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/win/winman.h"

namespace hype::win {

    namespace {

        bool desk_built = false;

    }

    hcpp::u32 desk_open_app(const char* name) noexcept {
        return wm_open(name, hype::ui::Rect{});
    }

    hcpp::u32 desk_window_count() noexcept { return wm_count(); }

    hcpp::u32 desk_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return 0;
        }
        if (!desk_built) {
            desk_built = true;
            desk_open_app("HxAbout");
            desk_open_app("HxSettings");
        }
        hype::ui::Canvas canvas(fb);
        const hype::ui::Theme& theme = hype::ui::theme_active();
        const hype::ui::Rect screen{
            0, 0, static_cast<hcpp::i32>(fb.width()),
            static_cast<hcpp::i32>(fb.height())};
        canvas.set_clip(screen);
        if (!desk_bg_paint(fb)) {
            hype::gfx::gfx_gradient_v(canvas, screen, theme.bg,
                                      hype::ui::rui_shade(theme.bg, 10));
        }
        wm_paint(fb);
        return wm_count();
    }

}
