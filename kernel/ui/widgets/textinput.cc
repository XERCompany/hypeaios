#include "kernel/ui/widgets/textinput.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    TextInput::TextInput(hcpp::StringView placeholder) noexcept
        : placeholder_(placeholder) {}

    void TextInput::set_masked(bool masked) noexcept { masked_ = masked; }

    bool TextInput::masked() const noexcept { return masked_; }

    void TextInput::set_placeholder(hcpp::StringView placeholder) noexcept {
        placeholder_ = placeholder;
    }

    hcpp::StringView TextInput::placeholder() const noexcept {
        return placeholder_;
    }

    void TextInput::set_focused(bool focused) noexcept { focused_ = focused; }

    bool TextInput::focused() const noexcept { return focused_; }

    void TextInput::set_padding(Insets padding) noexcept { padding_ = padding; }

    Insets TextInput::padding() const noexcept { return padding_; }

    bool TextInput::push_char(char c) noexcept {
        if (length_ >= capacity || c < 0x20 || c > 0x7E) {
            return false;
        }
        buffer_[length_++] = c;
        return true;
    }

    bool TextInput::backspace() noexcept {
        if (length_ == 0) {
            return false;
        }
        buffer_[--length_] = '\0';
        return true;
    }

    void TextInput::clear() noexcept {
        length_ = 0;
        buffer_[0] = '\0';
    }

    const char* TextInput::data() const noexcept { return buffer_; }

    hcpp::size_t TextInput::length() const noexcept { return length_; }

    hcpp::StringView TextInput::text() const noexcept {
        return hcpp::StringView(buffer_, length_);
    }

    Size TextInput::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolve_font(nullptr);
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            desired = {240, line + padding_.vertical() + 2};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void TextInput::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        canvas.fill_rect(box, canvas.color(theme.card));
        canvas.stroke_rect(
            box, canvas.color(focused_ ? theme.ring : theme.border));

        const hcpp::fonts::FontFace* face = resolve_font(nullptr);
        if (face == nullptr) {
            return;
        }
        const Rect inner = box.inset(padding_);
        hcpp::i32 offset = (box.h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = box.y + offset + face->baseline;

        char dots[capacity];
        hcpp::StringView shown;
        if (length_ == 0) {
            shown = placeholder_;
        } else if (masked_) {
            for (hcpp::size_t i = 0; i < length_; ++i) {
                dots[i] = '*';
            }
            shown = hcpp::StringView(dots, length_);
        } else {
            shown = text();
        }
        const hcpp::u32 text_rgb =
            length_ == 0 ? theme.muted_fg : theme.fg;
        hcpp::i32 text_w = 0;
        if (!shown.empty()) {
            text_w = text_extent(shown, *face).width;
            canvas.draw_text(inner.x, baseline_y, shown,
                             canvas.color(text_rgb), *face);
        }
        if (focused_) {
            hcpp::i32 caret_x = inner.x + text_w + 1;
            if (caret_x > inner.right() - 1) {
                caret_x = inner.right() - 1;
            }
            canvas.fill_rect({caret_x, inner.y + 1, 1, inner.h - 2},
                             canvas.color(theme.fg));
        }
    }

}  // namespace hype::ui
