#include "kernel/ui/hyperui.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::i32 isqrt_i32(hcpp::i32 value) noexcept {
            if (value <= 0) {
                return 0;
            }
            const hcpp::u32 v = static_cast<hcpp::u32>(value);
            hcpp::u32 x = v;
            hcpp::u32 y = (x + 1) / 2;
            while (y < x) {
                x = y;
                y = (x + v / x) / 2;
            }
            return static_cast<hcpp::i32>(x);
        }

        hcpp::i32 clamp_i32(hcpp::i32 value, hcpp::i32 low, hcpp::i32 high) noexcept {
            if (value < low) {
                return low;
            }
            return value > high ? high : value;
        }

        hcpp::i32 clamp_radius(Rect rect, hcpp::i32 radius) noexcept {
            const hcpp::i32 half = (rect.w < rect.h ? rect.w : rect.h) / 2;
            if (radius < 0) {
                return 0;
            }
            return radius > half ? half : radius;
        }

        hcpp::u8 chan_lerp(hcpp::u8 a, hcpp::u8 b, hcpp::u32 t) noexcept {
            return static_cast<hcpp::u8>((a * (255 - t) + b * t) / 255);
        }

        void unpack_rgb(hcpp::u32 rgb, hcpp::u8& r, hcpp::u8& g, hcpp::u8& b) noexcept {
            r = static_cast<hcpp::u8>((rgb >> 16) & 0xFF);
            g = static_cast<hcpp::u8>((rgb >> 8) & 0xFF);
            b = static_cast<hcpp::u8>(rgb & 0xFF);
        }

        hcpp::i32 row_inset(Rect rect, hcpp::i32 radius, hcpp::i32 row) noexcept {
            const hcpp::i32 top_center = rect.y + radius;
            const hcpp::i32 bottom_center = rect.bottom() - radius - 1;
            hcpp::i32 distance = 0;
            if (row < top_center) {
                distance = top_center - row;
            } else if (row > bottom_center) {
                distance = row - bottom_center;
            }
            if (distance <= 0) {
                return 0;
            }
            return radius - isqrt_i32(radius * radius - distance * distance);
        }

        void fill_span_alpha(Canvas& canvas, hcpp::i32 x0, hcpp::i32 x1,
                             hcpp::i32 y, hcpp::u32 color,
                             hcpp::u8 alpha) noexcept {
            if (x1 <= x0) {
                return;
            }
            canvas.fill_rect_alpha(Rect{x0, y, x1 - x0, 1}, color, alpha);
        }

        // Dark-UI depth rule: when the card surface is dark, elevation is
        // expressed through luminosity tiers, not black drop shadows.
        bool dark_surface() noexcept {
            const Theme& theme = theme_active();
            hcpp::u8 r = 0;
            hcpp::u8 g = 0;
            hcpp::u8 b = 0;
            unpack_rgb(theme.card, r, g, b);
            return static_cast<hcpp::u32>(r) + g + b < 384;
        }

        void fill_rounded_alpha(Canvas& canvas, Rect rect, hcpp::i32 radius,
                                const Rect& cut, hcpp::u32 color,
                                hcpp::u8 alpha) noexcept {
            if (rect.empty() || alpha == 0) {
                return;
            }
            const hcpp::i32 r = clamp_radius(rect, radius);
            const Rect area = rect.intersection(canvas.clip());
            for (hcpp::i32 row = area.y; row < area.bottom(); ++row) {
                const hcpp::i32 inset = r > 0 ? row_inset(rect, r, row) : 0;
                const hcpp::i32 sx = rect.x + inset;
                const hcpp::i32 sr = rect.right() - inset;
                if (row >= cut.y && row < cut.bottom()) {
                    fill_span_alpha(canvas, sx,
                                    cut.x < sr ? cut.x : sr, row, color, alpha);
                    fill_span_alpha(canvas,
                                    cut.right() > sx ? cut.right() : sx, sr,
                                    row, color, alpha);
                } else {
                    fill_span_alpha(canvas, sx, sr, row, color, alpha);
                }
            }
        }

    }  // namespace

    bool rui_dark_surface() noexcept { return dark_surface(); }

    hcpp::u32 rui_lift(hcpp::u32 rgb, hcpp::u32 elevation) noexcept {
        hcpp::u8 r = 0;
        hcpp::u8 g = 0;
        hcpp::u8 b = 0;
        unpack_rgb(rgb, r, g, b);
        const hcpp::u32 lift = elevation * 6 > 48 ? 48 : elevation * 6;
        const hcpp::u32 nr = r + lift > 255 ? 255 : r + lift;
        const hcpp::u32 ng = g + lift > 255 ? 255 : g + lift;
        const hcpp::u32 nb = b + lift > 255 ? 255 : b + lift;
        return (nr << 16) | (ng << 8) | nb;
    }

    hcpp::u32 rui_shade(hcpp::u32 rgb, hcpp::u32 amount) noexcept {
        hcpp::u8 r = 0;
        hcpp::u8 g = 0;
        hcpp::u8 b = 0;
        unpack_rgb(rgb, r, g, b);
        const hcpp::u32 nr = amount > r ? 0 : r - amount;
        const hcpp::u32 ng = amount > g ? 0 : g - amount;
        const hcpp::u32 nb = amount > b ? 0 : b - amount;
        return (nr << 16) | (ng << 8) | nb;
    }

    void rui_rounded_gradient(Canvas& canvas, Rect rect, hcpp::i32 radius,
                              hcpp::u32 top_rgb, hcpp::u32 bottom_rgb) noexcept {
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        hcpp::u8 r1 = 0;
        hcpp::u8 g1 = 0;
        hcpp::u8 b1 = 0;
        hcpp::u8 r2 = 0;
        hcpp::u8 g2 = 0;
        hcpp::u8 b2 = 0;
        unpack_rgb(top_rgb, r1, g1, b1);
        unpack_rgb(bottom_rgb, r2, g2, b2);
        const hcpp::i32 r = clamp_radius(rect, radius);
        const hcpp::i32 span = rect.h > 4 ? rect.h * 2 / 5 : 1;
        for (hcpp::i32 row = area.y; row < area.bottom(); ++row) {
            const hcpp::u32 t = static_cast<hcpp::u32>(
                clamp_i32((row - rect.y) * 255 / span, 0, 255));
            const hcpp::u32 color = canvas.color(
                (static_cast<hcpp::u32>(chan_lerp(r1, r2, t)) << 16) |
                (static_cast<hcpp::u32>(chan_lerp(g1, g2, t)) << 8) |
                static_cast<hcpp::u32>(chan_lerp(b1, b2, t)));
            const hcpp::i32 inset = r > 0 ? row_inset(rect, r, row) : 0;
            canvas.fill_rect(Rect{rect.x + inset, row, rect.w - 2 * inset, 1},
                             color);
        }
    }

    void rui_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius, hcpp::i32 blur,
                    hcpp::u8 alpha) noexcept {
        if (rect.empty() || alpha == 0) {
            return;
        }
        blur = clamp_i32(blur, 0, 40);
        const hcpp::i32 rw = rect.w + 2 * blur;
        const hcpp::i32 rh = rect.h + 2 * blur;
        if (rw <= 240 && rw * rh <= 60000) {
            gfx::gfx_soft_shadow_tinted(canvas, rect, radius, blur, alpha,
                                        theme_active().shadow);
            return;
        }
        const hcpp::u32 shadow =
            canvas.color(theme_active().shadow);
        const hcpp::i32 off = 2 + blur / 3;
        const Rect outer =
            rect.inset(Insets::uniform(-blur)).translated(Point{0, off + 1});
        const Rect mid =
            rect.inset(Insets::uniform(-blur / 2)).translated(Point{0, off});
        const Rect base = rect.translated(Point{0, off});
        fill_rounded_alpha(canvas, outer, radius + blur, rect, shadow,
                           static_cast<hcpp::u8>(alpha / 4));
        fill_rounded_alpha(canvas, mid, radius + blur / 2, rect, shadow,
                           static_cast<hcpp::u8>(alpha / 3));
        fill_rounded_alpha(canvas, base, radius, rect, shadow,
                           static_cast<hcpp::u8>(alpha / 2));
    }

    void rui_panel(Canvas& canvas, Rect rect, hcpp::u32 elevation) noexcept {
        if (rect.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        if (theme.hard_shadow) {
            draw_hard_shadow(canvas, rect, theme.card, theme.border);
            return;
        }
        if (elevation > rui_elev_window) {
            elevation = rui_elev_window;
        }
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_lg);
        const bool dark = dark_surface();
        if (elevation > 0) {
            // One big soft diffused shadow: wide blur, ~16% opacity.
            rui_shadow(canvas, rect, radius,
                       6 + static_cast<hcpp::i32>(elevation) * 4,
                       static_cast<hcpp::u8>(dark ? 18 + elevation * 5
                                                 : 24 + elevation * 14));
        }
        rui_rounded_gradient(canvas, rect, radius, rui_lift(theme.card, elevation),
                             theme.card);
        canvas.stroke_rounded_rect(rect, radius, canvas.color(theme.border));
    }

    void rui_card(Canvas& canvas, Rect rect) noexcept {
        rui_panel(canvas, rect, rui_elev_card);
    }

    void rui_control_bg(Canvas& canvas, Rect rect, hcpp::u32 fill_rgb,
                        hcpp::u32 border_rgb) noexcept {
        if (rect.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_md);
        const bool dark = dark_surface();
        // Dark surfaces get a faint soft shadow; luminosity does the lifting.
        rui_shadow(canvas, rect, radius, dark ? 8 : 4, dark ? 18 : 36);
        rui_rounded_gradient(canvas, rect, radius, rui_lift(fill_rgb, 1),
                             rui_shade(fill_rgb, 5));
        canvas.stroke_rounded_rect(rect, radius, canvas.color(border_rgb));
    }

}  // namespace hype::ui
