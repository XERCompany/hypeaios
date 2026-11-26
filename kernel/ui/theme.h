#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/types.hpp"

namespace hype::ui {

    struct Theme {
        hcpp::u32 background;
        hcpp::u32 surface;
        hcpp::u32 border;
        hcpp::u32 text_primary;
        hcpp::u32 text_muted;
        hcpp::u32 accent;
        hcpp::u32 on_accent;

        hcpp::u32 spacing_xs;
        hcpp::u32 spacing_sm;
        hcpp::u32 spacing_md;
        hcpp::u32 spacing_lg;

        hcpp::u32 radius_sm;
        hcpp::u32 radius_md;
        hcpp::u32 radius_lg;

        const hcpp::fonts::FontFace* font;

        const hcpp::fonts::FontFace* resolved_font() const noexcept {
            return font != nullptr ? font : hcpp::fonts::default_font();
        }

        static const Theme& dark() noexcept;
    };

}  // namespace hype::ui
