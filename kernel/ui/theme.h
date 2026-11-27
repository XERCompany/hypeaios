#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/types.hpp"

namespace hype::ui {

    struct Theme {
        hcpp::u32 bg;
        hcpp::u32 fg;
        hcpp::u32 card;
        hcpp::u32 card_fg;
        hcpp::u32 primary;
        hcpp::u32 primary_fg;
        hcpp::u32 secondary;
        hcpp::u32 destructive;
        hcpp::u32 success;
        hcpp::u32 warning;
        hcpp::u32 muted;
        hcpp::u32 muted_fg;
        hcpp::u32 border;
        hcpp::u32 ring;
        hcpp::u32 accent;
        hcpp::u32 shadow;

        hcpp::u32 chart[5];

        hcpp::u32 spacing_xs;
        hcpp::u32 spacing_sm;
        hcpp::u32 spacing_md;
        hcpp::u32 spacing_lg;

        hcpp::u32 radius_sm;
        hcpp::u32 radius_md;
        hcpp::u32 radius_lg;

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

    bool theme_hard_shadow() noexcept;
    void theme_set_hard_shadow(bool enabled) noexcept;
    hcpp::i32 hard_shadow_offset() noexcept;
    void draw_hard_shadow(Canvas& canvas, const Rect& box, hcpp::u32 fill_rgb,
                          hcpp::u32 border_rgb) noexcept;

    const hcpp::fonts::FontFace* resolve_font(
        const hcpp::fonts::FontFace* font) noexcept;

}  // namespace hype::ui
