#include "kernel/hxrt/apps/builtins.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "version_generated.h"

namespace hype::hxrt {

    namespace {

        void hxabout_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            hype::ui::Canvas canvas(*ctx.fb);
            canvas.set_clip(ctx.paint_clip());
            const hype::ui::Theme& theme = hype::ui::theme_active();
            const hype::ui::Rect& box = ctx.region;
            hype::ui::rui_panel(canvas, box, hype::ui::rui_elev_window);
            const hcpp::i32 x = box.x + 20;
            hcpp::i32 y = box.y + 36;
            canvas.draw_text(x, y, "HypeAI OS", canvas.color(theme.accent));
            y += 24;
            canvas.draw_text(x, y, "HxAbout - system info", canvas.color(theme.fg));
            y += 22;
            canvas.draw_text(x, y, "version " HYPEAI_VERSION_FULL,
                             canvas.color(theme.muted_fg));
            y += 22;
            canvas.draw_text(x, y, "hxrt runtime - .hxer app registry",
                             canvas.color(theme.muted_fg));
            y += 22;
            canvas.draw_text(x, y, "HypeAICode++ x86_64 freestanding kernel",
                             canvas.color(theme.muted_fg));
        }

        void hxabout_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "HxAbout", "HYPEOS://S/apps/HxAbout.hxersys",
            hxabout_entry, hxabout_close, nullptr};

    }

    const HxerHeader* hxabout_app() noexcept { return &desc; }

}
