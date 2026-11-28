#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {
    class Canvas;
}

// HypeUI design tokens (issues #143, #144): a clean modern design system
// — flat surfaces, 1px hairline borders, and multi-stop SOFT shadows
// doing the elevation work (shadow-xs..2xl, like CSS shadow scales).
// No neon glows, no heavy colored outer glow.
namespace hype::ui::hui {

    // Color roles. Two tables exist — light and dark — mirroring the
    // active Theme's surface luminosity; use hui::colors() to fetch the
    // set that matches the current theme.
    struct HuiColors {
        hcpp::u32 bg;           // app background
        hcpp::u32 surface;      // card / panel surface
        hcpp::u32 surface_elev; // elevated surface (taskbar, flyouts)
        hcpp::u32 border;       // hairline border
        hcpp::u32 text;         // primary text
        hcpp::u32 text_dim;     // secondary text
        hcpp::u32 accent;       // sparse interactive accent (blue)
        hcpp::u32 accent_fg;    // text/icons on accent
        hcpp::u32 primary;      // primary button fill (near-black / near-white)
        hcpp::u32 primary_fg;   // text on primary buttons
        hcpp::u32 danger;
        hcpp::u32 success;
        hcpp::u32 shadow_rgb;   // shadow tint (near-black in both themes)
    };

    // One stop of the soft-shadow scale: vertical offset + blur + max
    // alpha (0-255). Mirrors CSS `shadow-xs..2xl`.
    struct HuiShadow {
        hcpp::i32 dy;
        hcpp::i32 blur;
        hcpp::u8 alpha;
    };

    enum class HuiShadowLevel : hcpp::u8 {
        Xs = 0,
        Sm,
        Md,
        Lg,
        Xl,
        Xxl,
    };

    // Radius scale: controls r6/r8, cards/windows r12.
    constexpr hcpp::i32 radius_sm = 6;
    constexpr hcpp::i32 radius_md = 8;
    constexpr hcpp::i32 radius_lg = 12;

    // Active token set: picks the light or dark table based on the
    // active Theme's card luminosity, so callers never branch on theme
    // themselves.
    const HuiColors& colors() noexcept;
    bool is_dark() noexcept;

    // Shadow scale lookup.
    const HuiShadow& shadow(HuiShadowLevel level) noexcept;

    // Draws a shadow stop for `rect` (rounded `radius`). In dark theme
    // the same stop renders slightly stronger and is paired by callers
    // with a 1px light top edge on the surface (Type-3 hybrid).
    // Respects theme.hard_shadow: hard mode renders a flat offset
    // duplicate instead of a blurred shadow.
    void draw_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius,
                     HuiShadowLevel level) noexcept;

    // 1px light top edge inside a rounded rect — the dark-theme "inner
    // glow line" that separates a card from its own shadow. No-op on
    // light theme (hairline border already does the job).
    void draw_top_edge(Canvas& canvas, Rect rect, hcpp::i32 radius) noexcept;

}  // namespace hype::ui::hui
