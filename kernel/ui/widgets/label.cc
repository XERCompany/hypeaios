#include "kernel/ui/widgets/label.h"

#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::u32 role_rgb(LabelColor role) noexcept {
            const Theme& theme = theme_active();
            switch (role) {
                case LabelColor::Muted:
                    return theme.muted_fg;
                case LabelColor::Accent:
                    return theme.accent;
                case LabelColor::Primary:
                default:
                    return theme.fg;
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

    void Label::set_text_size(hcpp::u32 size_px) noexcept { size_px_ = size_px; }

    hcpp::u32 Label::text_size() const noexcept { return size_px_; }

    const hcpp::fonts::FontFace* Label::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Size Label::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            if (size_px_ != 0 && hype::font::hypefont_ready()) {
                desired = Size{
                    text_sdf_measure(text_, size_px_),
                    static_cast<hcpp::i32>(
                        hype::font::text_line_height(size_px_))};
            } else {
                const hcpp::fonts::FontFace* face = resolved_font();
                desired = face != nullptr ? text_extent(text_, *face) : Size{};
            }
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
        if (text_.empty() || bounds().empty()) {
            return;
        }
        if (size_px_ != 0 && hype::font::hypefont_ready()) {
            const hcpp::i32 line = static_cast<hcpp::i32>(
                hype::font::text_line_height(size_px_));
            hcpp::i32 offset = (bounds().h - line) / 2;
            if (offset < 0) {
                offset = 0;
            }
            draw_text_sdf(canvas, bounds().x, bounds().y + offset, text_,
                          canvas.color(role_rgb(color_)), size_px_);
            return;
        }
        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr) {
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
