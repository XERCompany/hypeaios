#include "kernel/ui/widgets/item.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    Item::Item(hcpp::StringView title) noexcept : title_(title) {}

    void Item::set_icon(hcpp::StringView glyph) noexcept { icon_ = glyph; }

    hcpp::StringView Item::icon() const noexcept { return icon_; }

    void Item::set_title(hcpp::StringView title) noexcept { title_ = title; }

    hcpp::StringView Item::title() const noexcept { return title_; }

    void Item::set_description(hcpp::StringView description) noexcept {
        description_ = description;
    }

    hcpp::StringView Item::description() const noexcept { return description_; }

    void Item::set_action(hcpp::StringView label) noexcept { action_ = label; }

    hcpp::StringView Item::action() const noexcept { return action_; }

    void Item::set_icon_size(hcpp::i32 size) noexcept { icon_size_ = size; }

    hcpp::i32 Item::icon_size() const noexcept { return icon_size_; }

    void Item::set_padding(Insets padding) noexcept { padding_ = padding; }

    Insets Item::padding() const noexcept { return padding_; }

    void Item::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Item::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Item::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Rect Item::icon_rect() const noexcept {
        const Rect box = bounds();
        return {box.x + padding_.left,
                box.y + padding_.top +
                    (box.h - padding_.vertical() - icon_size_) / 2,
                icon_size_, icon_size_};
    }

    Rect Item::action_rect() const noexcept {
        const Rect box = bounds();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 width =
            face != nullptr && !action_.empty()
                ? text_extent(action_, *face).width
                : 0;
        return {box.right() - padding_.right - width,
                box.y + padding_.top, width,
                box.h - padding_.vertical()};
    }

    Size Item::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            const hcpp::i32 lines =
                !title_.empty() && !description_.empty() ? 2 : 1;
            hcpp::i32 text_h = lines * line;
            if (lines > 1) {
                text_h += 2;
            }
            desired = {160,
                       (icon_size_ > text_h ? icon_size_ : text_h) +
                           padding_.vertical()};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Item::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();

        hcpp::i32 text_x = box.x + padding_.left;
        if (icon_size_ > 0 && !icon_.empty()) {
            const Rect slot = icon_rect();
            canvas.fill_rounded_rect(
                slot, static_cast<hcpp::i32>(theme.radius_sm),
                canvas.color(theme.muted));
            if (face != nullptr) {
                const Size glyph = text_extent(icon_, *face);
                hcpp::i32 gx = slot.x + (slot.w - glyph.width) / 2;
                if (gx < slot.x) {
                    gx = slot.x;
                }
                hcpp::i32 offset = (slot.h - face->height) / 2;
                if (offset < 0) {
                    offset = 0;
                }
                canvas.draw_text(gx, slot.y + offset + face->baseline, icon_,
                                 canvas.color(theme.fg), *face);
            }
            text_x = slot.right() + static_cast<hcpp::i32>(theme.spacing_md);
        }
        if (face == nullptr) {
            return;
        }
        const hcpp::i32 text_right = action_rect().x -
            static_cast<hcpp::i32>(theme.spacing_md);
        const Rect saved = canvas.clip();
        canvas.set_clip(
            Rect{text_x, box.y,
                 (text_right > text_x ? text_right - text_x : 0), box.h}
                .intersection(saved));
        hcpp::i32 total_h = face->height;
        if (!description_.empty()) {
            total_h += face->height + 2;
        }
        hcpp::i32 top = box.y + (box.h - total_h) / 2;
        if (top < box.y + padding_.top) {
            top = box.y + padding_.top;
        }
        if (!title_.empty()) {
            canvas.draw_text(text_x, top + face->baseline, title_,
                             canvas.color(theme.fg), *face);
        }
        if (!description_.empty()) {
            canvas.draw_text(text_x, top + face->height + 2 + face->baseline,
                             description_, canvas.color(theme.muted_fg), *face);
        }
        canvas.set_clip(saved);
        if (!action_.empty()) {
            const Rect action = action_rect();
            hcpp::i32 offset = (action.h - face->height) / 2;
            if (offset < 0) {
                offset = 0;
            }
            canvas.draw_text(action.x, action.y + offset + face->baseline,
                             action_, canvas.color(theme.accent), *face);
        }
    }

}  // namespace hype::ui
