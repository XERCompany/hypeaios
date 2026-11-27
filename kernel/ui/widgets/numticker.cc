#include "kernel/ui/widgets/numticker.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::size_t write_i32(hcpp::i32 value, char* out,
                               hcpp::size_t capacity) noexcept {
            char digits[12];
            hcpp::size_t count = 0;
            hcpp::u32 magnitude;
            if (value < 0) {
                magnitude = static_cast<hcpp::u32>(-(value + 1)) + 1;
            } else {
                magnitude = static_cast<hcpp::u32>(value);
            }
            do {
                digits[count++] = static_cast<char>('0' + magnitude % 10);
                magnitude /= 10;
            } while (magnitude != 0 && count < sizeof(digits));
            hcpp::size_t written = 0;
            if (value < 0 && written < capacity) {
                out[written++] = '-';
            }
            while (count > 0 && written < capacity) {
                out[written++] = digits[--count];
            }
            return written;
        }

    }  // namespace

    NumberTicker::NumberTicker(hcpp::i32 target) noexcept : target_(target) {}

    void NumberTicker::set_target(hcpp::i32 target) noexcept {
        target_ = target;
    }

    hcpp::i32 NumberTicker::target() const noexcept { return target_; }

    void NumberTicker::set_value(hcpp::i32 value) noexcept { value_ = value; }

    hcpp::i32 NumberTicker::value() const noexcept { return value_; }

    void NumberTicker::set_step(hcpp::i32 step) noexcept {
        step_ = step != 0 ? step : 1;
    }

    hcpp::i32 NumberTicker::step() const noexcept { return step_; }

    void NumberTicker::set_prefix(hcpp::StringView prefix) noexcept {
        prefix_ = prefix;
    }

    void NumberTicker::set_suffix(hcpp::StringView suffix) noexcept {
        suffix_ = suffix;
    }

    bool NumberTicker::tick() noexcept {
        if (value_ == target_) {
            return false;
        }
        const hcpp::i32 direction = target_ > value_ ? 1 : -1;
        hcpp::i32 advance = step_ > 0 ? step_ : -step_;
        value_ += direction * advance;
        if ((direction > 0 && value_ > target_) ||
            (direction < 0 && value_ < target_)) {
            value_ = target_;
        }
        return true;
    }

    bool NumberTicker::tick(hcpp::u32) noexcept { return tick(); }

    bool NumberTicker::done() const noexcept { return value_ == target_; }

    void NumberTicker::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* NumberTicker::font() const noexcept {
        return font_;
    }

    const hcpp::fonts::FontFace* NumberTicker::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::size_t NumberTicker::format(char* out,
                                      hcpp::size_t capacity) const noexcept {
        hcpp::size_t written = 0;
        for (hcpp::size_t i = 0; i < prefix_.size() && written < capacity;
             ++i) {
            out[written++] = prefix_[i];
        }
        written += write_i32(value_, out + written, capacity - written);
        for (hcpp::size_t i = 0; i < suffix_.size() && written < capacity;
             ++i) {
            out[written++] = suffix_[i];
        }
        if (written < capacity) {
            out[written] = '\0';
        }
        return written;
    }

    Size NumberTicker::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            char buffer[24];
            const hcpp::size_t length = format(buffer, sizeof(buffer) - 1);
            const hcpp::i32 width =
                face != nullptr
                    ? text_extent(hcpp::StringView(buffer, length), *face).width
                    : 48;
            desired = {width, line};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void NumberTicker::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        const hcpp::fonts::FontFace* face = resolved_font();
        if (box.empty() || face == nullptr) {
            return;
        }
        const Theme& theme = theme_active();
        char buffer[24];
        const hcpp::size_t length = format(buffer, sizeof(buffer) - 1);
        const hcpp::StringView text(buffer, length);
        const hcpp::i32 text_w = text_extent(text, *face).width;
        hcpp::i32 x = box.x + (box.w - text_w) / 2;
        if (x < box.x) {
            x = box.x;
        }
        hcpp::i32 offset = (box.h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        canvas.draw_text(x, box.y + offset + face->baseline, text,
                         canvas.color(theme.fg), *face);
    }

}  // namespace hype::ui
