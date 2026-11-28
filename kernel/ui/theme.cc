#include "kernel/ui/theme.h"

#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"

namespace hype::ui {

    namespace {

        Theme& active_slot() noexcept {
            static Theme slot = theme_dark();
            return slot;
        }

        bool str_eq(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
            return a[i] == b[i];
        }

        bool surface_is_dark(const Theme& theme) noexcept {
            const hcpp::u32 card = theme.card;
            const hcpp::u32 sum = ((card >> 16) & 0xFF) +
                                  ((card >> 8) & 0xFF) + (card & 0xFF);
            return sum < 384;
        }

        // ui.accent palettes (issue #177): dark surfaces lighten the
        // accent on hover (GitHub-dark / VS Code convention), light
        // surfaces darken it. `soft` is the wash base used with
        // Theme::accent_soft_alpha; ring follows the accent so focus
        // rings track the chosen palette.
        struct AccentPalette {
            const char* name;
            hcpp::u32 rest;
            hcpp::u32 hover_dark;
            hcpp::u32 active_dark;
            hcpp::u32 hover_light;
            hcpp::u32 active_light;
            hcpp::u32 soft;
        };

        const AccentPalette accent_palettes[] = {
            {"blue", 0x2563EB, 0x3B82F6, 0x1D4ED8,
                     0x1D4ED8, 0x1E40AF, 0x3B82F6},
            {"violet", 0x7C3AED, 0x8B5CF6, 0x6D28D9,
                       0x6D28D9, 0x5B21B6, 0x8B5CF6},
            {"emerald", 0x059669, 0x10B981, 0x047857,
                        0x047857, 0x065F46, 0x10B981},
            {"rose", 0xE11D48, 0xF43F5E, 0xBE123C,
                     0xBE123C, 0x9F1239, 0xF43F5E},
        };

        const AccentPalette* accent_find(const char* name) noexcept {
            if (name == nullptr) {
                return nullptr;
            }
            for (const AccentPalette& palette : accent_palettes) {
                if (str_eq(palette.name, name)) {
                    return &palette;
                }
            }
            return nullptr;
        }

        const AccentPalette* active_accent = &accent_palettes[0];

        void apply_accent(Theme& theme) noexcept {
            const bool dark = surface_is_dark(theme);
            theme.accent = active_accent->rest;
            theme.accent_hover =
                dark ? active_accent->hover_dark : active_accent->hover_light;
            theme.accent_active =
                dark ? active_accent->active_dark : active_accent->active_light;
            theme.accent_soft = active_accent->soft;
            theme.ring = dark ? active_accent->hover_dark : active_accent->rest;
        }

    }  // namespace

    const Theme& theme_dark() noexcept {
        // HypeUI dark palette v2 (issue #177, spec §2.1): bg #0B0B0E,
        // surface #141418 / elevated #1B1B22, #26262C hairlines,
        // near-white primary buttons, single #2563EB accent. Elevation
        // is carried by the two-stop shadow scale + the 1px light top
        // edge (Type-3 hybrid).
        static const Theme theme{
            .bg = 0x0B0B0E,
            .fg = 0xFAFAFA,
            .card = 0x141418,
            .card_fg = 0xFAFAFA,
            .surface_sunk = 0x0E0E12,
            .primary = 0xF5F5F5,
            .primary_fg = 0x111318,
            .secondary = 0x1B1B22,
            .destructive = 0xEF4444,
            .destructive_soft = 0xEF4444,
            .destructive_soft_alpha = 36,  // ~14%
            .success = 0x22C55E,
            .warning = 0xEAB308,
            .muted = 0x16161C,
            .muted_fg = 0xA1A1AA,
            .border = 0x26262C,
            .border_strong = 0x3A3A44,
            .ring = 0x3B82F6,
            .ring_alpha = 140,             // ~55%
            .accent = 0x2563EB,
            .accent_fg = 0xFFFFFF,
            .accent_hover = 0x3B82F6,
            .accent_active = 0x1D4ED8,
            .accent_soft = 0x3B82F6,
            .accent_soft_alpha = 46,       // ~18%
            .scrim = 0x000000,
            .scrim_alpha = 140,            // ~55%
            .top_edge_alpha = 18,          // ~7% white top edge
            .shadow = 0x000000,
            .chart = {0x3B82F6, 0x22C55E, 0xEAB308, 0xEF4444, 0xA855F7},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_xs = 4,
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
        // HypeUI light palette v2 (spec §2.2): bg #F3F4F6, white cards,
        // #E4E4E7 hairlines, near-black primary buttons, same blue
        // accent with light-mode (darken-on-hover) stops.
        static const Theme theme{
            .bg = 0xF3F4F6,
            .fg = 0x18181B,
            .card = 0xFFFFFF,
            .card_fg = 0x18181B,
            .surface_sunk = 0xF4F4F5,
            .primary = 0x18181B,
            .primary_fg = 0xFFFFFF,
            .secondary = 0xFFFFFF,
            .destructive = 0xDC2626,
            .destructive_soft = 0xDC2626,
            .destructive_soft_alpha = 30,  // ~12%
            .success = 0x16A34A,
            .warning = 0xCA8A04,
            .muted = 0xF4F4F5,
            .muted_fg = 0x71717A,
            .border = 0xE4E4E7,
            .border_strong = 0xD4D4D8,
            .ring = 0x2563EB,
            .ring_alpha = 102,             // ~40%
            .accent = 0x2563EB,
            .accent_fg = 0xFFFFFF,
            .accent_hover = 0x1D4ED8,
            .accent_active = 0x1E40AF,
            .accent_soft = 0x3B82F6,
            .accent_soft_alpha = 30,       // ~12%
            .scrim = 0x000000,
            .scrim_alpha = 102,            // ~40%
            .top_edge_alpha = 0,           // unused on light surfaces
            .shadow = 0x000000,
            .chart = {0x3B82F6, 0x16A34A, 0xCA8A04, 0xDC2626, 0x9333EA},
            .spacing_xs = 2,
            .spacing_sm = 4,
            .spacing_md = 8,
            .spacing_lg = 16,
            .radius_xs = 4,
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
        // Re-stamp the active accent palette so ui.accent survives
        // dark<->light switches (issue #177).
        apply_accent(active_slot());
        // HypeUI marker: which token table is live (issues #143, #144).
        serial::write("hui theme=");
        serial::write_line(surface_is_dark(theme) ? "dark" : "light");
    }

    bool theme_accent_valid(const char* name) noexcept {
        return accent_find(name) != nullptr;
    }

    bool theme_set_accent(const char* name) noexcept {
        const AccentPalette* palette = accent_find(name);
        if (palette == nullptr) {
            return false;
        }
        active_accent = palette;
        apply_accent(active_slot());
        serial::write("theme accent=");
        serial::write_line(palette->name);
        return true;
    }

    const char* theme_accent_name() noexcept { return active_accent->name; }

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
