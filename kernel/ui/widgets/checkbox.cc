#include "kernel/ui/widgets/checkbox.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 box_size = 16;

    }  // namespace

    Checkbox::Checkbox(hcpp::StringView label) noexcept : label_(label) {}

    void Checkbox::set_label(hcpp::StringView label) noexcept { label_ = label; }

    hcpp::StringView Checkbox::label() const noexcept { return label_; }

    void Checkbox::set_checked(bool checked) noexcept { checked_ = checked; }

    bool Checkbox::checked() const noexcept { return checked_; }

    void Checkbox::toggle() noexcept { checked_ = !checked_; }

    void Checkbox::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Checkbox::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Checkbox::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    void Checkbox::set_focused(bool focused) noexcept { focused_ = focused; }

    bool Checkbox::focused() const noexcept { return focused_; }

    bool Checkbox::on_key(char key) noexcept {
        if (key == keys::space || key == '\n' || key == '\r') {
            toggle();
            return true;
        }
        return false;
    }

    Rect Checkbox::focus_ring_rect() const noexcept { return bounds(); }

    Rect Checkbox::box_rect() const noexcept {
        const Rect b = bounds();
        hcpp::i32 y = b.y + (b.h - box_size) / 2;
        if (y < b.y) {
            y = b.y;
        }
        return {b.x, y, box_size, box_size};
    }

    Size Checkbox::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const Size text =
                face != nullptr ? text_extent(label_, *face) : Size{};
            desired = {box_size + 6 + text.width,
                       box_size > text.height ? box_size : text.height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Checkbox::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect box = box_rect();
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_sm);
        if (checked_) {
            canvas.fill_rounded_rect(box, radius, canvas.color(theme.primary));
            const hcpp::u32 mark = canvas.color(theme.primary_fg);
            const hcpp::i32 x0 = box.x + 3;
            const hcpp::i32 x1 = box.x + box.w / 2 - 1;
            const hcpp::i32 x2 = box.right() - 3;
            const hcpp::i32 ym = box.y + box.h / 2;
            const hcpp::i32 yl = box.bottom() - 4;
            const hcpp::i32 yt = box.y + 4;
            canvas.draw_line(x0, ym, x1, yl, mark);
            canvas.draw_line(x1, yl, x2, yt, mark);
            canvas.draw_line(x0, ym + 1, x1, yl + 1, mark);
            canvas.draw_line(x1, yl + 1, x2, yt + 1, mark);
        } else {
            canvas.fill_rounded_rect(box, radius, canvas.color(theme.card));
        }
        canvas.stroke_rounded_rect(box, radius, canvas.color(theme.border));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face != nullptr && !label_.empty()) {
            hcpp::i32 offset = (b.h - face->height) / 2;
            if (offset < 0) {
                offset = 0;
            }
            canvas.draw_text(box.right() + 6, b.y + offset + face->baseline,
                             label_, canvas.color(theme.fg), *face);
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

}  // namespace hype::ui
