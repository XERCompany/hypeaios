#pragma once

#include "hcpp/types.hpp"

namespace hype::ui {

    // DPI scale factor in hundredths (issue #174): 100 = 1.00x. Derived
    // once at boot from the framebuffer height — 768 px is the 1.00
    // baseline, the raw ratio snaps to 0.25 steps inside [1.00, 3.00]
    // (720 -> 1.00, 900 -> 1.25, 1080 -> 1.50, 1600 -> 2.00).
    // ui_scale_init also rewrites the shell metric globals declared in
    // the deskbar/winman/deskicons headers (bar strip, caption strip,
    // cascade step, desktop icon grid + margin).
    hcpp::u32 ui_scale_x100() noexcept;
    void ui_scale_init(hcpp::u32 fb_w, hcpp::u32 fb_h) noexcept;
    // Test hook: override the factor (the widget selftest battery was
    // authored against 1.00x pixel expectations; stage_ui snapshots and
    // restores the real value around it).
    void ui_scale_set(hcpp::u32 scale_x100) noexcept;

    // base * scale / 100 rounded to the nearest px. The scale defaults
    // to 1.00 until ui_scale_init runs, so early-boot callers see the
    // unscaled base metrics.
    hcpp::i32 ui_px(hcpp::i32 base) noexcept;

    // Font-size variant for the SDF text stack (hypetext): clamps the
    // scaled size to the rasterizer's supported 1..128 px range
    // (ttfrast k_max_size_px).
    hcpp::u32 ui_text_px(hcpp::u32 base_px) noexcept;

}
