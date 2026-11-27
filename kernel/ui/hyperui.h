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
    void rui_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius, hcpp::i32 blur,
                    hcpp::u8 alpha) noexcept;
    void rui_rounded_gradient(Canvas& canvas, Rect rect, hcpp::i32 radius,
                              hcpp::u32 top_rgb, hcpp::u32 bottom_rgb) noexcept;
    void rui_panel(Canvas& canvas, Rect rect, hcpp::u32 elevation) noexcept;
    void rui_card(Canvas& canvas, Rect rect) noexcept;
    void rui_control_bg(Canvas& canvas, Rect rect, hcpp::u32 fill_rgb,
                        hcpp::u32 border_rgb) noexcept;

}  // namespace hype::ui
