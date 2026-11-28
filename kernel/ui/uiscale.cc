#include "kernel/ui/uiscale.h"

#include "kernel/serial.hpp"
#include "kernel/ui/deskbar.h"
#include "kernel/win/deskicons.h"
#include "kernel/win/winman.h"

namespace hype::ui {

    namespace {
        hcpp::u32 g_scale_x100 = 100;
    }

    hcpp::u32 ui_scale_x100() noexcept { return g_scale_x100; }

    void ui_scale_set(hcpp::u32 scale_x100) noexcept {
        g_scale_x100 = scale_x100;
    }

    hcpp::i32 ui_px(hcpp::i32 base) noexcept {
        return static_cast<hcpp::i32>(
            (static_cast<hcpp::i64>(base) *
                 static_cast<hcpp::i64>(g_scale_x100) + 50) / 100);
    }

    hcpp::u32 ui_text_px(hcpp::u32 base_px) noexcept {
        hcpp::u32 scaled = (base_px * g_scale_x100 + 50) / 100;
        if (scaled == 0) {
            scaled = 1;
        } else if (scaled > 128) {
            scaled = 128;
        }
        return scaled;
    }

    void ui_scale_init(hcpp::u32 fb_w, hcpp::u32 fb_h) noexcept {
        if (fb_h != 0) {
            // fb_h/768 rounded to the nearest 0.25 step, clamped to
            // [1.00, 3.00]. Exact halves round up (1050 -> 1.25,
            // 1080 -> 1.50, 1440 -> 1.75, 1600 -> 2.00).
            const hcpp::u32 raw_x100 = (fb_h * 100 + 384) / 768;
            const hcpp::u32 quarters = (raw_x100 * 4 + 50) / 100;
            hcpp::u32 snapped = quarters * 25;
            if (snapped < 100) {
                snapped = 100;
            } else if (snapped > 300) {
                snapped = 300;
            }
            g_scale_x100 = snapped;
        }

        // Shell metric subset (issue #174): bar strip, caption strip,
        // cascade step, desktop icon grid + margin. Everything that
        // derives from dims (hit zones, work area, toast/flyout
        // anchoring) follows automatically.
        deskbar_height = ui_px(36);
        hype::win::wm_chrome_height = ui_px(32);
        hype::win::wm_cascade_step = ui_px(40);
        hype::win::desk_icon_cell_w = ui_px(96);
        hype::win::desk_icon_cell_h = ui_px(88);
        hype::win::desk_icon_margin = ui_px(12);

        // Serial markers (issue #174): the mode VBE picked plus the
        // scale the shell metrics just took.
        serial::write("fb mode=");
        serial::write_dec32(fb_w);
        serial::write("x");
        serial::write_dec32(fb_h);
        serial::write("\r\n");
        serial::write("ui scale=");
        serial::write_dec32(g_scale_x100 / 100);
        serial::write(".");
        const hcpp::u32 frac = g_scale_x100 % 100;
        if (frac < 10) {
            serial::write("0");
        }
        serial::write_dec32(frac);
        serial::write("\r\n");
    }

}
