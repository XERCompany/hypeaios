#include "kernel/hxrt/apps/builtins.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/win/xau.h"
#include "version_generated.h"

namespace hype::hxrt {

    namespace {

        // Shared body: used by the .hxer fallback entry and the XAU
        // WndProc PAINT path below (region + clip in screen coords).
        void paint_about(hcpp::graphics::Framebuffer& fb,
                         const hype::ui::Rect& box,
                         const hype::ui::Rect& clip) noexcept {
            hype::ui::Canvas canvas(fb);
            canvas.set_clip(clip);
            const hype::ui::Theme& theme = hype::ui::theme_active();
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
            canvas.draw_text(x, y, "xau windowing api (WndProc)",
                             canvas.color(theme.muted_fg));
            y += 22;
            canvas.draw_text(x, y, "HypeAICode++ x86_64 freestanding kernel",
                             canvas.color(theme.muted_fg));
        }

        // XAU WndProc: the "HxAbout" window class renders and handles
        // input through the Windows-style message dispatch. wm windows
        // opened under the app name are adopted by the xau paint hook.
        hcpp::i32 hxabout_wndproc(hype::win::XAU hwnd, hcpp::u32 msg,
                                  hcpp::u64 wparam,
                                  hcpp::u64 lparam) noexcept {
            switch (msg) {
                case hype::win::XAU_MSG_PAINT: {
                    const hype::win::XauDC* dc =
                        reinterpret_cast<const hype::win::XauDC*>(lparam);
                    if (dc != nullptr && dc->fb != nullptr &&
                        dc->fb->valid() && !dc->client.empty()) {
                        paint_about(*dc->fb, dc->client, dc->clip);
                    }
                    return 0;
                }
                case hype::win::XAU_MSG_LBUTTONDOWN:
                    // Click in the client area repaints the window.
                    hype::win::xau_invalidate(hwnd, nullptr);
                    return 0;
                default:
                    return hype::win::xau_defproc(hwnd, msg, wparam,
                                                  lparam);
            }
        }

        void hxabout_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            paint_about(*ctx.fb, ctx.region, ctx.paint_clip());
        }

        void hxabout_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "HxAbout", "HYPEOS://S/apps/HxAbout.hxersys",
            hxabout_entry, hxabout_close, nullptr};

    }

    const HxerHeader* hxabout_app() noexcept { return &desc; }

    // Registers the "HxAbout" XAU class so wm windows of this app paint
    // and dispatch through hxabout_wndproc instead of the .hxer entry.
    void hxabout_register_xau() noexcept {
        hype::win::xau_register_class("HxAbout", &hxabout_wndproc, 0, 0);
    }

}
