// HypeUI tokens — see huitokens.h for the design rationale (issues #143, #144).
#include "kernel/ui/huitokens.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"

namespace hype::ui::hui {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Insets;
        using hype::ui::Point;
        using hype::ui::Rect;

        // Light table: bg #F3F4F6, white cards, #E5E7EB hairlines,
        // near-black primary buttons, sparse #3B82F6 accent.
        const HuiColors k_light{
            .bg = 0xF3F4F6,
            .surface = 0xFFFFFF,
            .surface_elev = 0xFFFFFF,
            .border = 0xE5E7EB,
            .text = 0x111827,
            .text_dim = 0x6B7280,
            .accent = 0x3B82F6,
            .accent_fg = 0xFFFFFF,
            .primary = 0x111827,
            .primary_fg = 0xFFFFFF,
            .danger = 0xDC2626,
            .success = 0x16A34A,
            .shadow_rgb = 0x000000,
        };

        // Dark table: bg #0F1113, cards #1A1D21 / elevated #22262B,
        // #2A2E34 hairlines, near-white primary buttons, same blue.
        const HuiColors k_dark{
            .bg = 0x0F1113,
            .surface = 0x1A1D21,
            .surface_elev = 0x22262B,
            .border = 0x2A2E34,
            .text = 0xF9FAFB,
            .text_dim = 0x9CA3AF,
            .accent = 0x3B82F6,
            .accent_fg = 0xFFFFFF,
            .primary = 0xF9FAFB,
            .primary_fg = 0x111827,
            .danger = 0xEF4444,
            .success = 0x22C55E,
            .shadow_rgb = 0x000000,
        };

        bool dark_active() noexcept {
            const Theme& theme = theme_active();
            const hcpp::u32 card = theme.card;
            const hcpp::u32 sum = ((card >> 16) & 0xFF) +
                                  ((card >> 8) & 0xFF) + (card & 0xFF);
            return sum < 384;
        }

    }  // namespace

    const HuiColors& colors() noexcept {
        return dark_active() ? k_dark : k_light;
    }

    bool is_dark() noexcept { return dark_active(); }

    const HuiShadow& shadow(HuiShadowLevel level) noexcept {
        // CSS-style soft-shadow stops (dy / blur / alpha as 0-255):
        //   xs:  y1  blur2  .04 | sm: y2  blur6  .06 | m:  y4  blur12 .08
        //   l:   y8  blur24 .10 | xl: y16 blur40 .12 | 2xl: y24 blur64 .14
        // Dark theme bumps each stop ~+25% alpha so the shadow still
        // reads against a dark background.
        static const HuiShadow scale[6] = {
            {1, 2, 10},   // xs  (.04 * 255 ≈ 10)
            {2, 6, 15},   // sm  (.06 * 255 ≈ 15)
            {4, 12, 20},  // m   (.08 * 255 ≈ 20)
            {8, 24, 26},  // l   (.10 * 255 ≈ 26)
            {16, 40, 31}, // xl  (.12 * 255 ≈ 31)
            {24, 64, 36}, // 2xl (.14 * 255 ≈ 36)
        };
        const hcpp::u8 idx = static_cast<hcpp::u8>(level);
        return scale[idx > 5 ? 5 : idx];
    }

    void draw_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius,
                     HuiShadowLevel level) noexcept {
        if (rect.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        if (theme.hard_shadow) {
            // Flat variant: offset duplicate, no blur (ui.hard_shadow).
            const hcpp::i32 off = 2 + shadow(level).dy;
            canvas.fill_rect(rect.translated(Point{off, off}),
                             canvas.color(theme.shadow));
            return;
        }
        const HuiShadow spec = shadow(level);
        hcpp::u32 alpha = spec.alpha;
        if (is_dark()) {
            alpha = alpha + alpha / 4 > 255 ? 255 : alpha + alpha / 4;
        }
        // gfx_soft_shadow derives its own offset from blur; pass the
        // token's dy by shifting the source rect down first so the cast
        // lands where the token says it should.
        const Rect shifted = rect.translated(
            Point{0, spec.dy - (2 + spec.blur / 3)});
        gfx::gfx_soft_shadow(canvas, shifted, radius, spec.blur,
                             static_cast<hcpp::u8>(alpha));
    }

    void draw_top_edge(Canvas& canvas, Rect rect, hcpp::i32 radius) noexcept {
        if (!is_dark() || rect.empty()) {
            return;
        }
        // Faint 1px white edge just inside the top of the shape — the
        // Type-3 hybrid's light line separating card from shadow.
        const hcpp::i32 inset = radius > 0 ? radius - 1 : 0;
        canvas.fill_rect_alpha(
            Rect{rect.x + inset + 2, rect.y + 1,
                 rect.w - 2 * (inset + 2), 1},
            canvas.color(0xFFFFFF), 18);
    }

}  // namespace hype::ui::hui
