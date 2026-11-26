#include "kernel/ui/theme.h"

namespace hype::ui {

    const Theme& Theme::dark() noexcept {
        static const Theme theme{
            .background = 0x0F1115,
            .surface = 0x1B1E26,
            .border = 0x32363F,
            .text_primary = 0xE8EAF0,
            .text_muted = 0x8B90A0,
            .accent = 0x4C8DFF,
            .on_accent = 0xFFFFFF,
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_sm = 2,
            .radius_md = 4,
            .radius_lg = 8,
            .font = nullptr,
        };
        return theme;
    }

    const hcpp::fonts::FontFace* Theme::resolved_font() const noexcept {
        return font != nullptr ? font : hcpp::fonts::default_font();
    }

    const hcpp::fonts::FontFace* resolve_font(
        const hcpp::fonts::FontFace* font) noexcept {
        return font != nullptr ? font : Theme::dark().resolved_font();
    }

}  // namespace hype::ui
