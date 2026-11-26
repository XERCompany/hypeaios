#include "kernel/ui/widgets/label.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::u32 role_rgb(LabelColor role) noexcept {
            const Theme& theme = Theme::dark();
            switch (role) {
                case LabelColor::Muted:
                    return theme.text_muted;
                case LabelColor::Accent:
                    return theme.accent;
                case LabelColor::Primary:
                default:
                    return theme.text_primary;
            }
        }

    }  // namespace

    Label::Label(hcpp::StringView text) noexcept : text_(text) {}

    void Label::set_text(hcpp::StringView text) noexcept { text_ = text; }

    hcpp::StringView Label::text() const noexcept { return text_; }

    void Label::set_color(LabelColor color) noexcept { color_ = color; }

    LabelColor Label::color() const noexcept { return color_; }

    void Label::set_font(const hcpp::fonts::FontFace* font) noexcept { font_ = font; }

    const hcpp::fonts::FontFace* Label::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Label::resolved_font() const noexcept {
        if (font_ != nullptr) {
            return font_;
        }
        return Theme::dark().resolved_font();
    }

    Size Label::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            desired = face != nullptr ? text_extent(text_, *face) : Size{};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Label::paint(Canvas& canvas) noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || text_.empty() || bounds().empty()) {
            return;
        }
        hcpp::i32 offset = (bounds().h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = bounds().y + offset + face->baseline;
        canvas.draw_text(bounds().x, baseline_y, text_, canvas.color(role_rgb(color_)),
                         *face);
    }

}  // namespace hype::ui
