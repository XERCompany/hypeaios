#include "kernel/ui/theme.h"

#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"

namespace hype::ui {

    namespace {

        Theme& active_slot() noexcept {
            static Theme slot = theme_dark();
            return slot;
        }

    }  // namespace

    const Theme& theme_dark() noexcept {
        // HypeUI dark palette (issues #143, #144): bg #0F1113, cards
        // #1A1D21 / elevated #22262B, #2A2E34 hairlines, near-white
        // primary buttons, sparse #3B82F6 accent. Elevation is carried
        // by soft shadows + the 1px light top edge (Type-3 hybrid).
        static const Theme theme{
            .bg = 0x0F1113,
            .fg = 0xF9FAFB,
            .card = 0x1A1D21,
            .card_fg = 0xF9FAFB,
            .primary = 0xF9FAFB,
            .primary_fg = 0x111827,
            .secondary = 0x22262B,
            .destructive = 0xEF4444,
            .success = 0x22C55E,
            .warning = 0xEAB308,
            .muted = 0x16191D,
            .muted_fg = 0x9CA3AF,
            .border = 0x2A2E34,
            .ring = 0x3B82F6,
            .accent = 0x3B82F6,
            .shadow = 0x000000,
            .chart = {0x3B82F6, 0x22C55E, 0xEAB308, 0xEF4444, 0xA855F7},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_sm = 6,
            .radius_md = 8,
            .radius_lg = 12,
            .text_size = 13,
            .font = nullptr,
            .hard_shadow = false,
        };
        return theme;
    }

    const Theme& theme_light() noexcept {
        // HypeUI light palette: bg #F3F4F6, white cards, #E5E7EB
        // hairlines, near-black primary buttons, sparse #3B82F6 accent.
        static const Theme theme{
            .bg = 0xF3F4F6,
            .fg = 0x111827,
            .card = 0xFFFFFF,
            .card_fg = 0x111827,
            .primary = 0x111827,
            .primary_fg = 0xFFFFFF,
            .secondary = 0xF3F4F6,
            .destructive = 0xDC2626,
            .success = 0x16A34A,
            .warning = 0xCA8A04,
            .muted = 0xE5E7EB,
            .muted_fg = 0x6B7280,
            .border = 0xE5E7EB,
            .ring = 0x3B82F6,
            .accent = 0x3B82F6,
            .shadow = 0x000000,
            .chart = {0x3B82F6, 0x16A34A, 0xCA8A04, 0xDC2626, 0x9333EA},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_sm = 6,
            .radius_md = 8,
            .radius_lg = 12,
            .text_size = 13,
            .font = nullptr,
            .hard_shadow = false,
        };
        return theme;
    }

    void theme_set(Theme theme) noexcept {
        active_slot() = theme;
        // HypeUI marker: which token table is live (issues #143, #144).
        const hcpp::u32 card = theme.card;
        const hcpp::u32 sum = ((card >> 16) & 0xFF) + ((card >> 8) & 0xFF) +
                              (card & 0xFF);
        serial::write("hui theme=");
        serial::write_line(sum < 384 ? "dark" : "light");
    }

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
