#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {
    class Canvas;
}

// HypeUI design tokens v2 (issues #143, #144, #177): a clean modern
// design system — flat surfaces, 1px hairline borders, and two-stop
// umbra+penumbra SOFT shadows doing the elevation work (shadow-xs..2xl,
// like CSS shadow scales). No neon glows, no heavy colored outer glow.
namespace hype::ui::hui {

    // Color roles — the paint-facing view of the active Theme. Both
    // light and dark variants are served from the same Theme fields,
    // so ui.accent and ui.theme updates propagate without a second
    // table. Colors are 0xRRGGBB; alpha channels ride in *_alpha.
    struct HuiColors {
        hcpp::u32 bg;           // app background
        hcpp::u32 surface;      // card / panel surface
        hcpp::u32 surface_sunk; // wells, code blocks, sunken tracks
        hcpp::u32 surface_elev; // elevated surface (taskbar, flyouts)
        hcpp::u32 border;       // hairline border
        hcpp::u32 border_strong;// input edges, hover borders
        hcpp::u32 text;         // primary text
        hcpp::u32 text_dim;     // secondary text
        hcpp::u32 accent;       // sparse interactive accent
        hcpp::u32 accent_fg;    // text/icons on accent
        hcpp::u32 accent_hover;
        hcpp::u32 accent_active;
        hcpp::u32 accent_soft;  // hover/selected wash base color
        hcpp::u8 accent_soft_alpha;
        hcpp::u32 primary;      // primary button fill
        hcpp::u32 primary_fg;   // text on primary buttons
        hcpp::u32 danger;
        hcpp::u32 danger_soft;  // danger wash base color
        hcpp::u8 danger_soft_alpha;
        hcpp::u32 success;
        hcpp::u32 warning;
        hcpp::u32 ring;         // focus ring color
        hcpp::u8 ring_alpha;
        hcpp::u32 scrim;        // modal backdrop dim color
        hcpp::u8 scrim_alpha;
        hcpp::u8 top_edge_alpha;// Type-3 top highlight alpha
        hcpp::u32 shadow_rgb;   // shadow tint (near-black in both themes)
    };

    // One stop of the soft-shadow scale: vertical offset + blur + max
    // alpha (0-255). Every elevation level is a two-stop recipe —
    // penumbra (wide, soft, low alpha) painted first, then umbra
    // (tight, sharp, higher alpha) — matching the spec §2.3 table.
    struct HuiShadow {
        hcpp::i32 dy;
        hcpp::i32 blur;
        hcpp::u8 alpha;         // dark-theme alpha; light scales down
    };

    struct HuiShadowRecipe {
        HuiShadow penumbra;     // alpha == 0 -> stop absent
        HuiShadow umbra;
    };

    enum class HuiShadowLevel : hcpp::u8 {
        Xs = 0,
        Sm,
        Md,
        Lg,
        Xl,
        Xxl,
    };

    // Radius scale: r4 chips/badges, r6 controls, r8 cards, r12 windows.
    constexpr hcpp::i32 radius_xs = 4;
    constexpr hcpp::i32 radius_sm = 6;
    constexpr hcpp::i32 radius_md = 8;
    constexpr hcpp::i32 radius_lg = 12;

    // Active token set: mirrors theme_active() so callers never branch
    // on theme themselves.
    const HuiColors& colors() noexcept;
    bool is_dark() noexcept;

    // Shadow recipe lookup: penumbra first (alpha 0 = skip), then umbra.
    const HuiShadowRecipe& shadow_recipe(HuiShadowLevel level) noexcept;

    // Draws the two-stop shadow for `rect` (rounded `radius`): soft
    // penumbra under a tight umbra. Alphas come straight from the §2.3
    // dark table; the light theme scales them down ~40%. Respects
    // theme.hard_shadow: hard mode renders a flat offset duplicate.
    void draw_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius,
                     HuiShadowLevel level) noexcept;

    // 1px light top edge inside a rounded rect — the dark-theme
    // Type-3 "inner glow line" separating a card from its shadow.
    // Alpha comes from theme.top_edge_alpha; no-op on light theme.
    void draw_top_edge(Canvas& canvas, Rect rect, hcpp::i32 radius) noexcept;

}  // namespace hype::ui::hui
