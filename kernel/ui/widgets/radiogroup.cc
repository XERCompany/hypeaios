#include "kernel/ui/widgets/radiogroup.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 circle_diameter = 14;
        constexpr hcpp::i32 row_gap = 4;

    }  // namespace

    bool RadioGroup::add_option(hcpp::StringView label) noexcept {
        if (count_ >= max_options) {
            return false;
        }
        options_[count_] = label;
        ++count_;
        return true;
    }

    hcpp::size_t RadioGroup::option_count() const noexcept { return count_; }

    hcpp::StringView RadioGroup::option_at(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return {};
        }
        return options_[index];
    }

    bool RadioGroup::select(hcpp::size_t index) noexcept {
        if (index >= count_ || index == selected_) {
            return false;
        }
        selected_ = index;
        return true;
    }

    hcpp::size_t RadioGroup::selected() const noexcept { return selected_; }

    void RadioGroup::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* RadioGroup::font() const noexcept {
        return font_;
    }

    const hcpp::fonts::FontFace* RadioGroup::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    void RadioGroup::set_focused(bool focused) noexcept { focused_ = focused; }

    bool RadioGroup::focused() const noexcept { return focused_; }

    bool RadioGroup::on_key(char key) noexcept {
        if (count_ == 0) {
            return false;
        }
        if (key == keys::up) {
            selected_ = (selected_ + count_ - 1) % count_;
            return true;
        }
        if (key == keys::down || key == keys::space || key == '\n' ||
            key == '\r') {
            selected_ = (selected_ + 1) % count_;
            return true;
        }
        return false;
    }

    Rect RadioGroup::focus_ring_rect() const noexcept { return bounds(); }

    hcpp::i32 RadioGroup::row_height() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        hcpp::i32 height =
            face != nullptr ? static_cast<hcpp::i32>(face->height) + row_gap
                            : circle_diameter + row_gap;
        return height > circle_diameter + 2 ? height : circle_diameter + 2;
    }

    Size RadioGroup::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            hcpp::i32 widest = 0;
            for (hcpp::size_t i = 0; i < count_; ++i) {
                const Size text =
                    face != nullptr ? text_extent(options_[i], *face) : Size{};
                if (text.width > widest) {
                    widest = text.width;
                }
            }
            desired = {circle_diameter + 6 + widest,
                       static_cast<hcpp::i32>(count_) * row_height()};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void RadioGroup::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 row_h = row_height();
        const hcpp::i32 radius = circle_diameter / 2;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            const hcpp::i32 row_y = b.y + static_cast<hcpp::i32>(i) * row_h;
            if (row_y + row_h > b.bottom()) {
                break;
            }
            const hcpp::i32 cy = row_y + row_h / 2;
            const hcpp::i32 cx = b.x + radius + 1;
            canvas.stroke_circle(cx, cy, radius, canvas.color(theme.border));
            if (i == selected_) {
                canvas.fill_circle(cx, cy, radius - 4,
                                   canvas.color(theme.primary));
                canvas.stroke_circle(cx, cy, radius,
                                     canvas.color(theme.primary));
            }
            if (face != nullptr && !options_[i].empty()) {
                hcpp::i32 offset = (row_h - face->height) / 2;
                if (offset < 0) {
                    offset = 0;
                }
                canvas.draw_text(cx + radius + 6, row_y + offset + face->baseline,
                                 options_[i], canvas.color(theme.fg), *face);
            }
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

}  // namespace hype::ui
