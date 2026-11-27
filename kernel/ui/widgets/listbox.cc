#include "kernel/ui/widgets/listbox.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 row_pad_y = 4;
        constexpr hcpp::i32 row_pad_x = 8;
        constexpr hcpp::i32 scrollbar_w = 3;

        char lower_ascii(char c) noexcept {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        }

    }  // namespace

    bool Listbox::add_option(hcpp::StringView label) noexcept {
        if (count_ >= max_options) {
            return false;
        }
        options_[count_] = label;
        ++count_;
        return true;
    }

    void Listbox::clear() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            options_[i] = {};
        }
        count_ = 0;
        highlighted_ = 0;
        selected_ = none;
        scroll_ = 0;
    }

    hcpp::size_t Listbox::option_count() const noexcept { return count_; }

    hcpp::StringView Listbox::option_at(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return {};
        }
        return options_[index];
    }

    void Listbox::set_visible_count(hcpp::size_t count) noexcept {
        if (count == 0) {
            count = 1;
        }
        if (count > max_options) {
            count = max_options;
        }
        visible_ = count;
        ensure_visible();
    }

    hcpp::size_t Listbox::visible_count() const noexcept { return visible_; }

    bool Listbox::highlight(hcpp::size_t index) noexcept {
        if (index >= count_) {
            return false;
        }
        highlighted_ = index;
        ensure_visible();
        return true;
    }

    hcpp::size_t Listbox::highlighted() const noexcept { return highlighted_; }

    hcpp::size_t Listbox::selected() const noexcept { return selected_; }

    void Listbox::reset() noexcept {
        highlighted_ = 0;
        selected_ = none;
        scroll_ = 0;
    }

    void Listbox::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Listbox::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Listbox::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    void Listbox::set_focused(bool focused) noexcept { focused_ = focused; }

    bool Listbox::focused() const noexcept { return focused_; }

    bool Listbox::on_key(char key) noexcept {
        if (count_ == 0) {
            return false;
        }
        if (key == keys::up) {
            return highlight((highlighted_ + count_ - 1) % count_);
        }
        if (key == keys::down) {
            return highlight((highlighted_ + 1) % count_);
        }
        if (key == '\n' || key == '\r') {
            selected_ = highlighted_;
            return true;
        }
        if (key >= 0x20 && key <= 0x7E) {
            const char want = lower_ascii(key);
            for (hcpp::size_t step = 1; step <= count_; ++step) {
                const hcpp::size_t index = (highlighted_ + step) % count_;
                if (!options_[index].empty() &&
                    lower_ascii(options_[index][0]) == want) {
                    highlight(index);
                    return true;
                }
            }
        }
        return false;
    }

    Rect Listbox::focus_ring_rect() const noexcept { return bounds(); }

    hcpp::i32 Listbox::row_height() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        return (face != nullptr ? static_cast<hcpp::i32>(face->height) : 16) +
               row_pad_y * 2;
    }

    void Listbox::ensure_visible() noexcept {
        if (highlighted_ < scroll_) {
            scroll_ = highlighted_;
        }
        if (highlighted_ >= scroll_ + visible_) {
            scroll_ = highlighted_ - visible_ + 1;
        }
    }

    Size Listbox::measure(Size available) noexcept {
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
            hcpp::size_t rows = count_ < visible_ ? count_ : visible_;
            if (rows == 0) {
                rows = 1;
            }
            desired = {widest + row_pad_x * 2 + scrollbar_w + 4,
                       static_cast<hcpp::i32>(rows) * row_height() + 2};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Listbox::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        canvas.fill_rect(b, canvas.color(theme.card));
        canvas.stroke_rect(b, canvas.color(focused_ ? theme.ring
                                                  : theme.border));

        const Rect inner{b.x + 1, b.y + 1, b.w - 2, b.h - 2};
        const hcpp::i32 row_h = row_height();
        const hcpp::size_t last =
            scroll_ + visible_ < count_ ? scroll_ + visible_ : count_;
        for (hcpp::size_t i = scroll_; i < last; ++i) {
            const hcpp::i32 row_y =
                inner.y + static_cast<hcpp::i32>(i - scroll_) * row_h;
            if (row_y + row_h > inner.bottom()) {
                break;
            }
            if (i == highlighted_) {
                canvas.fill_rect({inner.x, row_y, inner.w - scrollbar_w - 2,
                                  row_h},
                                 canvas.color(theme.secondary));
            }
            if (i == selected_) {
                canvas.fill_rect({inner.x, row_y + 1, 2, row_h - 2},
                                 canvas.color(theme.accent));
            }
            if (face != nullptr && !options_[i].empty()) {
                hcpp::i32 offset = (row_h - face->height) / 2;
                if (offset < 0) {
                    offset = 0;
                }
                canvas.draw_text(inner.x + row_pad_x,
                                 row_y + offset + face->baseline, options_[i],
                                 canvas.color(theme.fg), *face);
            }
        }
        if (count_ > visible_) {
            const Rect track{inner.right() - scrollbar_w, inner.y, scrollbar_w,
                             inner.h};
            canvas.fill_rect(track, canvas.color(theme.muted));
            hcpp::i32 thumb_h =
                track.h * static_cast<hcpp::i32>(visible_) /
                static_cast<hcpp::i32>(count_);
            if (thumb_h < 6) {
                thumb_h = 6;
            }
            const hcpp::i32 travel = track.h - thumb_h;
            const hcpp::i32 thumb_y =
                track.y + (count_ > visible_
                               ? travel * static_cast<hcpp::i32>(scroll_) /
                                     static_cast<hcpp::i32>(count_ - visible_)
                               : 0);
            canvas.fill_rect({track.x, thumb_y, track.w, thumb_h},
                             canvas.color(theme.muted_fg));
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

}  // namespace hype::ui
