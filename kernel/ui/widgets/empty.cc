#include "kernel/ui/widgets/empty.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 glyph_diameter = 28;
        constexpr hcpp::i32 cta_pad_x = 12;
        constexpr hcpp::i32 cta_pad_y = 5;

        void draw_centered(Canvas& canvas, hcpp::i32 center_x, hcpp::i32 baseline_y,
                           hcpp::StringView text, hcpp::u32 rgb,
                           const hcpp::fonts::FontFace& face) noexcept {
            if (text.empty()) {
                return;
            }
            hcpp::i32 x = center_x - text_extent(text, face).width / 2;
            if (x < canvas.clip().x) {
                x = canvas.clip().x;
            }
            canvas.draw_text(x, baseline_y, text, rgb, face);
        }

    }  // namespace

    EmptyState::EmptyState(hcpp::StringView title) noexcept : title_(title) {}

    void EmptyState::set_glyph(hcpp::StringView glyph) noexcept {
        glyph_ = glyph;
    }

    hcpp::StringView EmptyState::glyph() const noexcept { return glyph_; }

    void EmptyState::set_title(hcpp::StringView title) noexcept {
        title_ = title;
    }

    hcpp::StringView EmptyState::title() const noexcept { return title_; }

    void EmptyState::set_description(hcpp::StringView description) noexcept {
        description_ = description;
    }

    hcpp::StringView EmptyState::description() const noexcept {
        return description_;
    }

    void EmptyState::set_cta(hcpp::StringView label) noexcept { cta_ = label; }

    hcpp::StringView EmptyState::cta() const noexcept { return cta_; }

    void EmptyState::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* EmptyState::font() const noexcept {
        return font_;
    }

    const hcpp::fonts::FontFace* EmptyState::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 EmptyState::content_height() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = face != nullptr ? face->height : 16;
        const Theme& theme = theme_active();
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_md);
        hcpp::i32 height = 0;
        if (!glyph_.empty()) {
            height += glyph_diameter + gap;
        }
        if (!title_.empty()) {
            height += line + gap / 2;
        }
        if (!description_.empty()) {
            height += line + gap;
        }
        if (!cta_.empty()) {
            height += line + 2 * cta_pad_y;
        }
        return height;
    }

    Rect EmptyState::glyph_rect() const noexcept {
        const Rect box = bounds();
        const hcpp::i32 top =
            box.y + (box.h - content_height()) / 2;
        return {box.x + (box.w - glyph_diameter) / 2, top, glyph_diameter,
                glyph_diameter};
    }

    Rect EmptyState::cta_rect() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        if (cta_.empty() || face == nullptr) {
            return {};
        }
        const Rect box = bounds();
        const hcpp::i32 line = face->height;
        const hcpp::i32 width =
            text_extent(cta_, *face).width + 2 * cta_pad_x;
        const hcpp::i32 top =
            box.y + (box.h - content_height()) / 2 + content_height() -
            (line + 2 * cta_pad_y);
        return {box.x + (box.w - width) / 2, top, width, line + 2 * cta_pad_y};
    }

    Size EmptyState::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            desired = {200, content_height() + 16};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void EmptyState::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        const hcpp::fonts::FontFace* face = resolved_font();
        if (box.empty() || face == nullptr) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_md);
        const hcpp::i32 center_x = box.x + box.w / 2;
        hcpp::i32 cursor_y = box.y + (box.h - content_height()) / 2;
        if (cursor_y < box.y) {
            cursor_y = box.y;
        }

        if (!glyph_.empty()) {
            const Rect slot = glyph_rect();
            const hcpp::i32 cx = slot.x + slot.w / 2;
            const hcpp::i32 cy = slot.y + slot.h / 2;
            canvas.stroke_circle(cx, cy, glyph_diameter / 2 - 1,
                                 canvas.color(theme.border));
            draw_centered(canvas, cx, cy + face->baseline - face->height / 2,
                          glyph_, canvas.color(theme.muted_fg), *face);
            cursor_y = slot.bottom() + gap;
        }
        if (!title_.empty()) {
            draw_centered(canvas, center_x, cursor_y + face->baseline, title_,
                          canvas.color(theme.fg), *face);
            cursor_y += face->height + gap / 2;
        }
        if (!description_.empty()) {
            draw_centered(canvas, center_x, cursor_y + face->baseline,
                          description_, canvas.color(theme.muted_fg), *face);
            cursor_y += face->height + gap;
        }
        if (!cta_.empty()) {
            const Rect button = cta_rect();
            canvas.fill_rounded_rect(
                button, static_cast<hcpp::i32>(theme.radius_md),
                canvas.color(theme.primary));
            draw_centered(canvas, center_x,
                          button.y + cta_pad_y + face->baseline, cta_,
                          canvas.color(theme.primary_fg), *face);
        }
    }

}  // namespace hype::ui
