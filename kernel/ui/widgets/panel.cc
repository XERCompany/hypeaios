#include "kernel/ui/widgets/panel.h"

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
        return line + static_cast<hcpp::i32>(Theme::dark().spacing_md) + 4;
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
        const Theme& theme = Theme::dark();
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
        const Theme& theme = Theme::dark();
        canvas.fill_rect(box, canvas.color(theme.surface));
        canvas.stroke_rect(box, canvas.color(theme.border));

        const hcpp::i32 strip_h = title_bar_height();
        const Rect strip{box.x + 1, box.y + 1, box.w - 2, strip_h};
        if (!strip.empty()) {
            canvas.fill_rect(strip, canvas.color(theme.border));
            canvas.fill_rect({strip.x, strip.bottom() - 1, strip.w, 1},
                             canvas.color(theme.accent));
        }

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || title_.empty()) {
            return;
        }
        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);
        hcpp::i32 offset = (strip_h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = strip.y + offset + face->baseline;
        canvas.draw_text(strip.x + pad, baseline_y, title_,
                         canvas.color(theme.text_primary), *face);
    }

}  // namespace hype::ui
