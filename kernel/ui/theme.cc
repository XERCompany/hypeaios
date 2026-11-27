#include "kernel/ui/theme.h"

#include "kernel/ui/canvas.h"

namespace hype::ui {

    namespace {

        Theme& active_slot() noexcept {
            static Theme slot = theme_dark();
            return slot;
        }

    }  // namespace

    const Theme& theme_dark() noexcept {
        static const Theme theme{
            .bg = 0x0F1115,
            .fg = 0xE8EAF0,
            .card = 0x1B1E26,
            .card_fg = 0xE8EAF0,
            .primary = 0x4C8DFF,
            .primary_fg = 0xFFFFFF,
            .secondary = 0x1B1E26,
            .destructive = 0xF85149,
            .success = 0x3FB950,
            .warning = 0xD29922,
            .muted = 0x232830,
            .muted_fg = 0x8B90A0,
            .border = 0x32363F,
            .ring = 0x4C8DFF,
            .accent = 0x4C8DFF,
            .shadow = 0x000000,
            .chart = {0x4C8DFF, 0x3FB950, 0xD29922, 0xF85149, 0xB57EDC},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_sm = 2,
            .radius_md = 4,
            .radius_lg = 8,
            .text_size = 13,
            .font = nullptr,
            .hard_shadow = false,
        };
        return theme;
    }

    const Theme& theme_light() noexcept {
        static const Theme theme{
            .bg = 0xF4F5F7,
            .fg = 0x1B1E26,
            .card = 0xFFFFFF,
            .card_fg = 0x1B1E26,
            .primary = 0x2F6FED,
            .primary_fg = 0xFFFFFF,
            .secondary = 0xE9ECF2,
            .destructive = 0xD92D20,
            .success = 0x14804A,
            .warning = 0xB54708,
            .muted = 0xE4E7EC,
            .muted_fg = 0x5A6272,
            .border = 0xC9CEDA,
            .ring = 0x2F6FED,
            .accent = 0x2F6FED,
            .shadow = 0x000000,
            .chart = {0x2F6FED, 0x14804A, 0xB54708, 0xD92D20, 0x7A5AF8},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_sm = 2,
            .radius_md = 4,
            .radius_lg = 8,
            .text_size = 13,
            .font = nullptr,
            .hard_shadow = false,
        };
        return theme;
    }

    void theme_set(Theme theme) noexcept { active_slot() = theme; }

    const Theme& theme_active() noexcept { return active_slot(); }

    bool theme_hard_shadow() noexcept { return active_slot().hard_shadow; }

    void theme_set_hard_shadow(bool enabled) noexcept {
        active_slot().hard_shadow = enabled;
    }

    hcpp::i32 hard_shadow_offset() noexcept { return 4; }

    void draw_hard_shadow(Canvas& canvas, const Rect& box, hcpp::u32 fill_rgb,
                          hcpp::u32 border_rgb) noexcept {
        const Theme& theme = theme_active();
        const hcpp::i32 off = hard_shadow_offset();
        canvas.fill_rect({box.x + off, box.y + off, box.w, box.h},
                         canvas.color(theme.shadow));
        canvas.fill_rect(box, canvas.color(fill_rgb));
        canvas.stroke_rect(box, canvas.color(border_rgb), 2);
    }

    const hcpp::fonts::FontFace* Theme::resolved_font() const noexcept {
        return font != nullptr ? font : hcpp::fonts::default_font();
    }

    const hcpp::fonts::FontFace* resolve_font(
        const hcpp::fonts::FontFace* font) noexcept {
        return font != nullptr ? font : theme_active().resolved_font();
    }

}  // namespace hype::ui
