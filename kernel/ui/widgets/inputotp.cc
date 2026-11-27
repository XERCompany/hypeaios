#include "kernel/ui/widgets/inputotp.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 cell_gap = 6;
        constexpr hcpp::i32 cell_padding = 12;

    }  // namespace

    InputOTP::InputOTP(hcpp::size_t cells) noexcept { set_cells(cells); }

    bool InputOTP::set_cells(hcpp::size_t cells) noexcept {
        if (cells == 0 || cells > capacity) {
            return false;
        }
        cells_ = cells;
        if (length_ > cells_) {
            length_ = cells_;
        }
        return true;
    }

    hcpp::size_t InputOTP::cells() const noexcept { return cells_; }

    bool InputOTP::push_char(char c) noexcept {
        if (c < '0' || c > '9' || length_ >= cells_) {
            return false;
        }
        digits_[length_++] = c;
        return true;
    }

    bool InputOTP::backspace() noexcept {
        if (length_ == 0) {
            return false;
        }
        digits_[--length_] = '\0';
        return true;
    }

    void InputOTP::clear() noexcept {
        for (hcpp::size_t i = 0; i < length_; ++i) {
            digits_[i] = '\0';
        }
        length_ = 0;
    }

    const char* InputOTP::data() const noexcept { return digits_; }

    hcpp::size_t InputOTP::length() const noexcept { return length_; }

    hcpp::StringView InputOTP::value() const noexcept {
        return hcpp::StringView(digits_, length_);
    }

    bool InputOTP::complete() const noexcept { return length_ == cells_; }

    hcpp::size_t InputOTP::active() const noexcept {
        return length_ < cells_ ? length_ : cells_ - 1;
    }

    void InputOTP::set_focused(bool focused) noexcept { focused_ = focused; }

    bool InputOTP::focused() const noexcept { return focused_; }

    bool InputOTP::on_key(char key) noexcept {
        if (key == '\b' || key == '\x7F') {
            return backspace();
        }
        return push_char(key);
    }

    Rect InputOTP::focus_ring_rect() const noexcept { return bounds(); }

    hcpp::i32 InputOTP::cell_side() const noexcept {
        const hcpp::fonts::FontFace* face = resolve_font(nullptr);
        const hcpp::i32 side =
            face != nullptr ? static_cast<hcpp::i32>(face->height) + cell_padding
                            : 28;
        return side < 20 ? 20 : side;
    }

    Rect InputOTP::cell_rect(hcpp::size_t index) const noexcept {
        const Rect b = bounds();
        const hcpp::i32 side = cell_side();
        hcpp::i32 y = b.y + (b.h - side) / 2;
        if (y < b.y) {
            y = b.y;
        }
        return {b.x + static_cast<hcpp::i32>(index) * (side + cell_gap), y,
                side, side};
    }

    Size InputOTP::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::i32 side = cell_side();
            desired = {static_cast<hcpp::i32>(cells_) * side +
                           static_cast<hcpp::i32>(cells_ - 1) * cell_gap,
                       side};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void InputOTP::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolve_font(nullptr);
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_md);
        const hcpp::size_t active_cell = active();
        for (hcpp::size_t i = 0; i < cells_; ++i) {
            const Rect cell = cell_rect(i);
            if (cell.x + cell.w > b.right()) {
                break;
            }
            const bool is_active =
                focused_ && i == active_cell && !complete();
            canvas.fill_rounded_rect(cell, radius, canvas.color(theme.card));
            canvas.stroke_rounded_rect(
                cell, radius,
                canvas.color(is_active ? theme.ring : theme.border));
            if (face != nullptr && i < length_) {
                const hcpp::StringView digit(digits_ + i, 1);
                const Size text = text_extent(digit, *face);
                hcpp::i32 offset = (cell.h - face->height) / 2;
                if (offset < 0) {
                    offset = 0;
                }
                canvas.draw_text(cell.x + (cell.w - text.width) / 2,
                                 cell.y + offset + face->baseline, digit,
                                 canvas.color(theme.fg), *face);
            }
            if (is_active) {
                canvas.fill_rect(
                    {cell.x + cell.w / 2, cell.y + 4, 1, cell.h - 8},
                    canvas.color(theme.fg));
            }
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

}  // namespace hype::ui
