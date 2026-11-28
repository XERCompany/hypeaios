// HypeUI tokens v2 — see huitokens.h for the design rationale
// (issues #143, #144, #177; spec docs/research/hypeui-design.md §2).
#include "kernel/ui/huitokens.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"

namespace hype::ui::hui {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Point;
        using hype::ui::Rect;

        bool dark_active() noexcept {
            const Theme& theme = theme_active();
            const hcpp::u32 card = theme.card;
            const hcpp::u32 sum = ((card >> 16) & 0xFF) +
                                  ((card >> 8) & 0xFF) + (card & 0xFF);
            return sum < 384;
        }

        // Light surfaces need far less shadow alpha to read — scale
        // the dark-table stop down to ~40% (Tailwind v4-ish .10-.14).
        hcpp::u8 stop_alpha(hcpp::u8 alpha) noexcept {
            if (alpha == 0) {
                return 0;
            }
            if (dark_active()) {
                return alpha;
            }
            const hcpp::u32 scaled = static_cast<hcpp::u32>(alpha) * 2 / 5;
            return static_cast<hcpp::u8>(scaled == 0 ? 1 : scaled);
        }

        void draw_stop(Canvas& canvas, Rect rect, hcpp::i32 radius,
                       const HuiShadow& stop, hcpp::u32 shadow_rgb) noexcept {
            const hcpp::u8 alpha = stop_alpha(stop.alpha);
            if (alpha == 0) {
                return;
            }
            // gfx_soft_shadow derives its own drop from the blur
            // (2 + blur / 3); shift the source rect so the cast lands
            // where the token's dy says it should.
            const Rect shifted = rect.translated(
                Point{0, stop.dy - (2 + stop.blur / 3)});
            gfx::gfx_soft_shadow_tinted(canvas, shifted, radius, stop.blur,
                                        alpha, shadow_rgb);
        }

    }  // namespace

    const HuiColors& colors() noexcept {
        // The Theme is the single token table: hui colors are a
        // projected view, so ui.theme / ui.accent changes reach every
        // hui::colors() consumer with no second copy to keep in sync.
        static HuiColors active{};
        const Theme& theme = theme_active();
        active = HuiColors{
            .bg = theme.bg,
            .surface = theme.card,
            .surface_sunk = theme.surface_sunk,
            .surface_elev = theme.secondary,
            .border = theme.border,
            .border_strong = theme.border_strong,
            .text = theme.fg,
            .text_dim = theme.muted_fg,
            .accent = theme.accent,
            .accent_fg = theme.accent_fg,
            .accent_hover = theme.accent_hover,
            .accent_active = theme.accent_active,
            .accent_soft = theme.accent_soft,
            .accent_soft_alpha = theme.accent_soft_alpha,
            .primary = theme.primary,
            .primary_fg = theme.primary_fg,
            .danger = theme.destructive,
            .danger_soft = theme.destructive_soft,
            .danger_soft_alpha = theme.destructive_soft_alpha,
            .success = theme.success,
            .warning = theme.warning,
            .ring = theme.ring,
            .ring_alpha = theme.ring_alpha,
            .scrim = theme.scrim,
            .scrim_alpha = theme.scrim_alpha,
            .top_edge_alpha = theme.top_edge_alpha,
            .shadow_rgb = theme.shadow,
        };
        return active;
    }

    bool is_dark() noexcept { return dark_active(); }

    const HuiShadowRecipe& shadow_recipe(HuiShadowLevel level) noexcept {
        // Two-stop recipes (penumbra, umbra) as (dy / blur / alpha):
        //   xs:  --            | (1, 2, .28)   buttons, inputs, badges
        //   sm:  (1, 3, .12)   | (1, 2, .18)   cards, taskbar, panels
        //   md:  (4, 8, .14)   | (1, 3, .18)   raised card, sticky head
        //   lg:  (8, 16, .16)  | (2, 6, .20)   menus, popovers, toasts
        //   xl:  (16, 28, .20) | (4, 10, .24)  dialogs, sheets, palette
        //   2xl: (24, 44, .30) | (8, 16, .28)  windows, drag-lifted
        // Alphas are the spec's dark-surface values (~Tailwind v4
        // x2-2.5); the light theme scales them in draw_shadow.
        static const HuiShadowRecipe scale[6] = {
            {{0, 0, 0},     {1, 2, 71}},   // xs
            {{1, 3, 31},    {1, 2, 46}},   // sm
            {{4, 8, 36},    {1, 3, 46}},   // md
            {{8, 16, 41},   {2, 6, 51}},   // lg
            {{16, 28, 51},  {4, 10, 61}},  // xl
            {{24, 44, 77},  {8, 16, 71}},  // 2xl
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
            const HuiShadowRecipe& recipe = shadow_recipe(level);
            const hcpp::i32 dy =
                recipe.umbra.alpha != 0 ? recipe.umbra.dy
                                        : recipe.penumbra.dy;
            const hcpp::i32 off = 2 + dy;
            canvas.fill_rect(rect.translated(Point{off, off}),
                             canvas.color(theme.shadow));
            return;
        }
        const HuiShadowRecipe& recipe = shadow_recipe(level);
        const hcpp::u32 shadow_rgb = colors().shadow_rgb;
        draw_stop(canvas, rect, radius, recipe.penumbra, shadow_rgb);
        draw_stop(canvas, rect, radius, recipe.umbra, shadow_rgb);
    }

    void draw_top_edge(Canvas& canvas, Rect rect, hcpp::i32 radius) noexcept {
        const hcpp::u8 alpha = theme_active().top_edge_alpha;
        if (!is_dark() || rect.empty() || alpha == 0) {
            return;
        }
        // Faint 1px white edge just inside the top of the shape — the
        // Type-3 hybrid's light line separating card from shadow.
        const hcpp::i32 inset = radius > 0 ? radius - 1 : 0;
        canvas.fill_rect_alpha(
            Rect{rect.x + inset + 2, rect.y + 1,
                 rect.w - 2 * (inset + 2), 1},
            canvas.color(0xFFFFFF), alpha);
    }

}  // namespace hype::ui::hui
