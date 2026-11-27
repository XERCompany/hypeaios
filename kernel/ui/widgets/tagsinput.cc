#include "kernel/ui/widgets/tagsinput.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 chip_gap = 4;
        constexpr hcpp::i32 chip_pad_x = 8;
        constexpr hcpp::i32 chip_pad_y = 3;

    }  // namespace

    bool TagsInput::add_tag(hcpp::StringView text) noexcept {
        if (text.empty() || tag_count_ >= max_tags) {
            return false;
        }
        hcpp::size_t length = text.size();
        if (length > max_tag_length) {
            length = max_tag_length;
        }
        for (hcpp::size_t i = 0; i < length; ++i) {
            tags_[tag_count_][i] = text[i];
        }
        tag_lengths_[tag_count_] = length;
        ++tag_count_;
        return true;
    }

    bool TagsInput::remove_last() noexcept {
        if (tag_count_ == 0) {
            return false;
        }
        --tag_count_;
        tag_lengths_[tag_count_] = 0;
        return true;
    }

    void TagsInput::clear() noexcept {
        tag_count_ = 0;
        pending_length_ = 0;
        pending_[0] = '\0';
    }

    hcpp::size_t TagsInput::tag_count() const noexcept { return tag_count_; }

    hcpp::StringView TagsInput::tag_at(hcpp::size_t index) const noexcept {
        if (index >= tag_count_) {
            return {};
        }
        return hcpp::StringView(tags_[index], tag_lengths_[index]);
    }

    bool TagsInput::push_char(char c) noexcept {
        if (pending_length_ >= buffer_capacity - 1 || c < 0x20 || c > 0x7E) {
            return false;
        }
        pending_[pending_length_++] = c;
        pending_[pending_length_] = '\0';
        return true;
    }

    bool TagsInput::backspace() noexcept {
        if (pending_length_ > 0) {
            pending_[--pending_length_] = '\0';
            return true;
        }
        return remove_last();
    }

    bool TagsInput::commit_pending() noexcept {
        if (pending_length_ == 0) {
            return false;
        }
        if (!add_tag(hcpp::StringView(pending_, pending_length_))) {
            return false;
        }
        pending_length_ = 0;
        pending_[0] = '\0';
        return true;
    }

    hcpp::StringView TagsInput::pending() const noexcept {
        return hcpp::StringView(pending_, pending_length_);
    }

    void TagsInput::set_placeholder(hcpp::StringView placeholder) noexcept {
        placeholder_ = placeholder;
    }

    hcpp::StringView TagsInput::placeholder() const noexcept {
        return placeholder_;
    }

    void TagsInput::set_focused(bool focused) noexcept { focused_ = focused; }

    bool TagsInput::focused() const noexcept { return focused_; }

    bool TagsInput::on_key(char key) noexcept {
        if (key == '\n' || key == '\r') {
            return commit_pending();
        }
        if (key == '\b' || key == '\x7F') {
            return backspace();
        }
        return push_char(key);
    }

    Rect TagsInput::focus_ring_rect() const noexcept { return bounds(); }

    Size TagsInput::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolve_font(nullptr);
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            desired = {240, line + chip_pad_y * 2 + padding_.vertical() + 2};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void TagsInput::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolve_font(nullptr);
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_md);
        canvas.fill_rounded_rect(box, radius, canvas.color(theme.card));
        canvas.stroke_rounded_rect(
            box, radius,
            canvas.color(focused_ ? theme.ring : theme.border));

        const Rect inner = box.inset(padding_);
        hcpp::i32 cursor_x = inner.x;
        if (face != nullptr) {
            const hcpp::i32 chip_h =
                static_cast<hcpp::i32>(face->height) + chip_pad_y * 2;
            hcpp::i32 chip_y = inner.y + (inner.h - chip_h) / 2;
            if (chip_y < inner.y) {
                chip_y = inner.y;
            }
            const hcpp::i32 baseline = chip_y + chip_pad_y + face->baseline;
            for (hcpp::size_t i = 0; i < tag_count_; ++i) {
                const hcpp::StringView tag = tag_at(i);
                const Size text = text_extent(tag, *face);
                const hcpp::i32 chip_w = text.width + chip_pad_x * 2;
                if (cursor_x + chip_w > inner.right()) {
                    break;
                }
                const Rect chip{cursor_x, chip_y, chip_w, chip_h};
                canvas.fill_rounded_rect(chip, chip_h / 2,
                                         canvas.color(theme.secondary));
                canvas.stroke_rounded_rect(chip, chip_h / 2,
                                           canvas.color(theme.border));
                canvas.draw_text(cursor_x + chip_pad_x, baseline, tag,
                                 canvas.color(theme.fg), *face);
                cursor_x += chip_w + chip_gap;
            }
            hcpp::i32 text_w = 0;
            const hcpp::StringView shown =
                tag_count_ == 0 && pending_length_ == 0 ? placeholder_
                                                        : pending();
            const hcpp::u32 text_rgb =
                pending_length_ == 0 && tag_count_ == 0 ? theme.muted_fg
                                                        : theme.fg;
            if (!shown.empty() && cursor_x < inner.right()) {
                text_w = text_extent(shown, *face).width;
                canvas.draw_text(cursor_x, baseline, shown,
                                 canvas.color(text_rgb), *face);
            }
            if (focused_) {
                hcpp::i32 caret_x = cursor_x + text_w + 1;
                if (caret_x > inner.right() - 1) {
                    caret_x = inner.right() - 1;
                }
                canvas.fill_rect({caret_x, chip_y + 2, 1, chip_h - 4},
                                 canvas.color(theme.fg));
            }
        }
        if (focused_) {
            draw_focus_ring(canvas, box);
        }
    }

}  // namespace hype::ui
