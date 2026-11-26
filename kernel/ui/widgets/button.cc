#include "kernel/ui/widgets/button.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    Button::Button(hcpp::StringView label) noexcept : label_(label) {}

    void Button::set_label(hcpp::StringView label) noexcept { label_ = label; }

    hcpp::StringView Button::label() const noexcept { return label_; }

    void Button::set_style(ButtonStyle style) noexcept { style_ = style; }

    ButtonStyle Button::style() const noexcept { return style_; }

    void Button::set_padding(Insets padding) noexcept { padding_ = padding; }

    Insets Button::padding() const noexcept { return padding_; }

    void Button::set_font(const hcpp::fonts::FontFace* font) noexcept { font_ = font; }

    const hcpp::fonts::FontFace* Button::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Button::resolved_font() const noexcept {
        if (font_ != nullptr) {
            return font_;
        }
        return Theme::dark().resolved_font();
    }

    Size Button::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const Size text = face != nullptr ? text_extent(label_, *face) : Size{};
            desired = {text.width + padding_.horizontal(),
                       text.height + padding_.vertical()};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Button::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = Theme::dark();
        const hcpp::u32 fill_rgb =
            style_ == ButtonStyle::Accent ? theme.accent : theme.surface;
        const hcpp::u32 text_rgb =
            style_ == ButtonStyle::Accent ? theme.on_accent : theme.text_primary;
        canvas.fill_rect(box, canvas.color(fill_rgb));
        canvas.stroke_rect(box, canvas.color(theme.border));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || label_.empty()) {
            return;
        }
        const Size text = text_extent(label_, *face);
        hcpp::i32 text_x = box.x + (box.w - text.width) / 2;
        if (text_x < box.x) {
            text_x = box.x;
        }
        hcpp::i32 offset = (box.h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = box.y + offset + face->baseline;
        canvas.draw_text(text_x, baseline_y, label_, canvas.color(text_rgb), *face);
    }

}  // namespace hype::ui
