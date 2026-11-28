#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    class Canvas;

    constexpr hcpp::u32 rui_elev_flat = 0;
    constexpr hcpp::u32 rui_elev_control = 1;
    constexpr hcpp::u32 rui_elev_card = 2;
    constexpr hcpp::u32 rui_elev_panel = 3;
    constexpr hcpp::u32 rui_elev_window = 4;

    hcpp::u32 rui_lift(hcpp::u32 rgb, hcpp::u32 elevation) noexcept;
    hcpp::u32 rui_shade(hcpp::u32 rgb, hcpp::u32 amount) noexcept;
    // True when the active card surface is dark: elevation is expressed via
    // luminosity tiers and accent inner-glow edges instead of dark shadows.
    bool rui_dark_surface() noexcept;
    // Worst-case soft-shadow spill beyond a widget's nominal rect:
    // rui_shadow clamps blur to 40 px on every side and drops the shadow
    // down by 2 + blur / 3 (≤ 15 px). Damage/invalidate rects for anything
    // painted via rui_panel/rui_card/rui_control_bg must be grown by this
    // or shadow pixels are left stale when the widget moves or closes.
    constexpr hcpp::i32 rui_shadow_spill = 42;
    constexpr hcpp::i32 rui_shadow_spill_down = 56;
    inline Rect rui_paint_bounds(Rect rect) noexcept {
        if (rect.empty()) {
            return {};
        }
        return rect.inset(Insets{-rui_shadow_spill, -rui_shadow_spill,
                                 -rui_shadow_spill, -rui_shadow_spill_down});
    }
    void rui_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius, hcpp::i32 blur,
                    hcpp::u8 alpha) noexcept;
    void rui_rounded_gradient(Canvas& canvas, Rect rect, hcpp::i32 radius,
                              hcpp::u32 top_rgb, hcpp::u32 bottom_rgb) noexcept;
    void rui_panel(Canvas& canvas, Rect rect, hcpp::u32 elevation) noexcept;
    void rui_card(Canvas& canvas, Rect rect) noexcept;
    void rui_control_bg(Canvas& canvas, Rect rect, hcpp::u32 fill_rgb,
                        hcpp::u32 border_rgb) noexcept;

}  // namespace hype::ui
