#include "kernel/ui/widgets/field.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 row_gap = 4;
        constexpr hcpp::i32 slot_inset = 2;

    }  // namespace

    Field::Field(hcpp::StringView label) noexcept : label_(label) {}

    void Field::set_label(hcpp::StringView label) noexcept { label_ = label; }

    hcpp::StringView Field::label() const noexcept { return label_; }

    void Field::set_description(hcpp::StringView description) noexcept {
        description_ = description;
    }

    hcpp::StringView Field::description() const noexcept { return description_; }

    void Field::set_error(hcpp::StringView error) noexcept { error_ = error; }

    hcpp::StringView Field::error() const noexcept { return error_; }

    bool Field::has_error() const noexcept { return !error_.empty(); }

    void Field::clear_error() noexcept { error_ = {}; }

    bool Field::set_control(Widget* control, Focusable* key_target) noexcept {
        if (control_ != nullptr) {
            remove_child(control_);
        }
        control_ = nullptr;
        key_target_ = nullptr;
        if (control == nullptr || !add_child(control)) {
            return false;
        }
        control_ = control;
        key_target_ = key_target;
        return true;
    }

    Widget* Field::control() const noexcept { return control_; }

    Focusable* Field::key_target() const noexcept { return key_target_; }

    void Field::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Field::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Field::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 Field::line_height() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        return face != nullptr ? static_cast<hcpp::i32>(face->height) : 16;
    }

    hcpp::StringView Field::hint_text() const noexcept {
        return has_error() ? error_ : description_;
    }

    void Field::set_focused(bool focused) noexcept {
        focused_ = focused;
        if (key_target_ != nullptr) {
            key_target_->set_focused(focused);
        }
    }

    bool Field::focused() const noexcept { return focused_; }

    bool Field::on_key(char key) noexcept {
        return key_target_ != nullptr && key_target_->on_key(key);
    }

    Rect Field::focus_ring_rect() const noexcept {
        if (key_target_ != nullptr) {
            return key_target_->focus_ring_rect();
        }
        return control_ != nullptr ? control_->bounds() : bounds();
    }

    Rect Field::slot_rect() const noexcept {
        const Rect b = bounds();
        const hcpp::i32 line = line_height();
        hcpp::i32 top = b.y;
        if (!label_.empty()) {
            top += line + row_gap;
        }
        hcpp::i32 bottom = b.bottom();
        if (!hint_text().empty()) {
            bottom -= line + row_gap;
        }
        hcpp::i32 h = bottom - top;
        if (h < 0) {
            h = 0;
        }
        return {b.x, top, b.w, h};
    }

    Size Field::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const hcpp::i32 line = line_height();
            const hcpp::StringView hint = hint_text();
            Size control_size{};
            if (control_ != nullptr) {
                control_size = control_->measure(available);
            }
            const Size label_size =
                face != nullptr && !label_.empty()
                    ? text_extent(label_, *face)
                    : Size{};
            const Size hint_size =
                face != nullptr && !hint.empty()
                    ? text_extent(hint, *face)
                    : Size{};
            hcpp::i32 width = control_size.width + slot_inset * 2;
            if (label_size.width > width) {
                width = label_size.width;
            }
            if (hint_size.width > width) {
                width = hint_size.width;
            }
            hcpp::i32 height = control_size.height + slot_inset * 2;
            if (!label_.empty()) {
                height += line + row_gap;
            }
            if (!hint.empty()) {
                height += line + row_gap;
            }
            desired = {width, height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Field::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        if (control_ == nullptr) {
            return;
        }
        control_->layout_tree(
            slot_rect().inset(Insets::uniform(slot_inset)));
    }

    void Field::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        if (face != nullptr && !label_.empty()) {
            canvas.draw_text(b.x, b.y + face->baseline, label_,
                             canvas.color(theme.fg), *face);
        }
        const hcpp::StringView hint = hint_text();
        if (face != nullptr && !hint.empty()) {
            const hcpp::i32 baseline =
                b.bottom() - (line_height() - face->baseline);
            const hcpp::u32 hint_rgb =
                has_error() ? theme.destructive : theme.muted_fg;
            canvas.draw_text(b.x, baseline, hint, canvas.color(hint_rgb),
                             *face);
        }
        if (has_error()) {
            const Rect slot = slot_rect();
            if (!slot.empty()) {
                canvas.stroke_rounded_rect(
                    slot, static_cast<hcpp::i32>(theme.radius_md),
                    canvas.color(theme.destructive));
            }
        }
    }

}  // namespace hype::ui
