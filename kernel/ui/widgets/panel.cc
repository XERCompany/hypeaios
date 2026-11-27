#include "kernel/ui/widgets/panel.h"

#include "kernel/font/hypefont.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    Panel::Panel(hcpp::StringView title) noexcept : title_(title) {}

    void Panel::set_title(hcpp::StringView title) noexcept { title_ = title; }

    hcpp::StringView Panel::title() const noexcept { return title_; }

    void Panel::set_title_bar_height(hcpp::i32 height) noexcept {
        title_height_ = height;
    }

    void Panel::set_font(const hcpp::fonts::FontFace* font) noexcept { font_ = font; }

    const hcpp::fonts::FontFace* Panel::font() const noexcept { return font_; }

    hcpp::i32 Panel::title_bar_height() const noexcept {
        if (title_height_ > 0) {
            return title_height_;
        }
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = face != nullptr ? face->height : 16;
        return line + static_cast<hcpp::i32>(theme_active().spacing_md) + 4;
    }

    const hcpp::fonts::FontFace* Panel::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Rect Panel::content_rect() const noexcept {
        const Rect box = bounds();
        const hcpp::i32 top = title_bar_height() + 1;
        return {box.x + 1, box.y + top, box.w - 2, box.h - top - 1};
    }

    Size Panel::measure(Size available) noexcept {
        const Theme& theme = theme_active();
        const hcpp::i32 top = title_bar_height() + 1;
        const Size content_available{
            available.width < 0 ? -1 : available.width - 2,
            available.height < 0 ? -1 : available.height - top - 1};

        Size kids{};
        for (hcpp::size_t i = 0; i < child_count(); ++i) {
            Widget* child = child_at(i);
            if (child == nullptr || !child->is_visible()) {
                continue;
            }
            const Size child_size = child->measure(content_available);
            if (child_size.width > kids.width) {
                kids.width = child_size.width;
            }
            if (child_size.height > kids.height) {
                kids.height = child_size.height;
            }
        }

        const hcpp::fonts::FontFace* face = resolved_font();
        const Size text =
            face != nullptr ? text_extent(title_, *face) : Size{};
        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);

        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            hcpp::i32 width = kids.width + 2;
            if (text.width + 2 * pad + 2 > width) {
                width = text.width + 2 * pad + 2;
            }
            desired = {width, top + 1 + kids.height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Panel::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        const Rect content = content_rect();
        for (hcpp::size_t i = 0; i < child_count(); ++i) {
            Widget* child = child_at(i);
            if (child != nullptr && child->is_visible()) {
                child->layout_tree(content);
            }
        }
    }

    void Panel::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        rui_panel(canvas, box, rui_elev_window);
        if (rui_dark_surface()) {
            hype::gfx::gfx_inner_glow_edge(
                canvas, box.inset(Insets::uniform(1)),
                static_cast<hcpp::i32>(theme.radius_lg), theme.accent);
        }

        const hcpp::i32 strip_h = title_bar_height();
        const Rect strip{box.x + 1, box.y + 1, box.w - 2, strip_h};
        if (!strip.empty()) {
            if (theme.hard_shadow) {
                canvas.fill_rect(strip, canvas.color(theme.border));
                canvas.fill_rect({strip.x, strip.bottom() - 1, strip.w, 1},
                                 canvas.color(theme.primary));
            } else {
                const hcpp::i32 radius =
                    static_cast<hcpp::i32>(theme.radius_lg);
                hcpp::i32 band_h = strip.h + radius;
                const hcpp::i32 max_h = box.bottom() - 1 - strip.y;
                if (band_h > max_h) {
                    band_h = max_h;
                }
                rui_rounded_gradient(
                    canvas, Rect{strip.x, strip.y, strip.w, band_h}, radius,
                    rui_lift(theme.muted, 1), theme.muted);
                canvas.fill_rounded_rect(
                    Rect{strip.x + 4, strip.bottom() - 1, strip.w - 8, 2}, 1,
                    canvas.color(theme.primary));
            }
        }

        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);
        const hcpp::i32 chrome_cy = strip.y + strip.h / 2;
        const hcpp::u32 chrome_rgbs[3] = {theme.destructive, theme.warning,
                                          theme.success};
        hcpp::i32 chrome_cx = strip.right() - pad - 4;
        for (hcpp::size_t i = 0; i < 3; ++i) {
            canvas.fill_circle(chrome_cx, chrome_cy, 4,
                               canvas.color(rui_shade(chrome_rgbs[i], 40)));
            chrome_cx -= 14;
        }
        canvas.fill_circle(strip.x + pad + 3, chrome_cy, 3,
                           canvas.color(theme.accent));

        if (title_.empty()) {
            return;
        }
        const hcpp::u32 title_px = theme.text_size + 2;
        if (hype::font::hypefont_ready() && title_px >= 10) {
            const hcpp::i32 line = static_cast<hcpp::i32>(
                hype::font::text_line_height(title_px));
            hcpp::i32 top = strip.y + (strip.h - line) / 2;
            if (top < strip.y) {
                top = strip.y;
            }
            draw_text_sdf_styled(canvas, strip.x + pad + 12, top, title_,
                                 canvas.color(theme.card_fg), title_px, 96,
                                 false);
            return;
        }
        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr) {
            return;
        }
        hcpp::i32 offset = (strip_h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = strip.y + offset + face->baseline;
        canvas.draw_text(strip.x + pad + 12, baseline_y, title_,
                         canvas.color(theme.card_fg), *face);
    }

}  // namespace hype::ui
