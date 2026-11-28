#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/types.hpp"

namespace hype::ui {

    // Token layer v2 (issue #177, docs/research/hypeui-design.md §2):
    // shadcn-style semantic ramp — bg < surface(card) < elevated
    // (secondary) carry the surface *kind*; depth comes from the
    // two-stop shadow scale plus the Type-3 top-edge highlight.
    // Colors are 0xRRGGBB; alpha channels live in the *_alpha u8s.
    struct Theme {
        hcpp::u32 bg;
        hcpp::u32 fg;
        hcpp::u32 card;
        hcpp::u32 card_fg;
        hcpp::u32 surface_sunk;  // wells, code blocks, zebra/sunk tracks
        hcpp::u32 primary;
        hcpp::u32 primary_fg;
        hcpp::u32 secondary;     // elevated surface (flyouts, taskbar)
        hcpp::u32 destructive;
        hcpp::u32 destructive_soft;   // danger wash base color
        hcpp::u8 destructive_soft_alpha;
        hcpp::u32 success;
        hcpp::u32 warning;
        hcpp::u32 muted;
        hcpp::u32 muted_fg;
        hcpp::u32 border;
        hcpp::u32 border_strong;      // input edges, hover borders
        hcpp::u32 ring;
        hcpp::u8 ring_alpha;          // focus ring painted alpha
        hcpp::u32 accent;
        hcpp::u32 accent_fg;          // text/glyphs on accent fills
        hcpp::u32 accent_hover;
        hcpp::u32 accent_active;
        hcpp::u32 accent_soft;        // hover/selected wash base color
        hcpp::u8 accent_soft_alpha;
        hcpp::u32 scrim;              // modal backdrop dim color
        hcpp::u8 scrim_alpha;
        hcpp::u8 top_edge_alpha;      // Type-3 inner top highlight alpha
        hcpp::u32 shadow;

        hcpp::u32 chart[5];

        hcpp::u32 spacing_xs;
        hcpp::u32 spacing_sm;
        hcpp::u32 spacing_md;
        hcpp::u32 spacing_lg;

        hcpp::u32 radius_xs;     // r4: badges, chips, kbd, scroll thumbs
        hcpp::u32 radius_sm;
        hcpp::u32 radius_md;
        hcpp::u32 radius_lg;

        hcpp::u32 text_size;

        const hcpp::fonts::FontFace* font;

        bool hard_shadow;

        const hcpp::fonts::FontFace* resolved_font() const noexcept;
    };

    class Canvas;
    struct Rect;

    const Theme& theme_dark() noexcept;
    const Theme& theme_light() noexcept;
    void theme_set(Theme theme) noexcept;
    const Theme& theme_active() noexcept;

    // Accent palettes (ui.accent, issue #177): a fixed set of named
    // ramps — blue/violet/emerald/rose — each carrying rest/hover/
    // active/soft stops for both dark and light surfaces. Setting an
    // accent rewrites the accent_* + ring fields of the active theme
    // and survives theme_set (re-stamped on every variant switch).
    // Prints "theme accent=<name>". Returns false for unknown names.
    bool theme_accent_valid(const char* name) noexcept;
    bool theme_set_accent(const char* name) noexcept;
    const char* theme_accent_name() noexcept;

    bool theme_hard_shadow() noexcept;
    void theme_set_hard_shadow(bool enabled) noexcept;
    hcpp::i32 hard_shadow_offset() noexcept;
    void draw_hard_shadow(Canvas& canvas, const Rect& box, hcpp::u32 fill_rgb,
                          hcpp::u32 border_rgb) noexcept;

    const hcpp::fonts::FontFace* resolve_font(
        const hcpp::fonts::FontFace* font) noexcept;

}  // namespace hype::ui
