#include "kernel/ui/widgets/switch.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 track_w = 36;
        constexpr hcpp::i32 track_h = 20;

    }  // namespace

    Switch::Switch(hcpp::StringView label) noexcept : label_(label) {}

    void Switch::set_label(hcpp::StringView label) noexcept { label_ = label; }

    hcpp::StringView Switch::label() const noexcept { return label_; }

    void Switch::set_on(bool on) noexcept {
        on_ = on;
        position_ = on ? knob_steps : 0;
    }

    bool Switch::on() const noexcept { return on_; }

    void Switch::toggle() noexcept { set_on(!on_); }

    void Switch::tick() noexcept {
        if (on_ && position_ < knob_steps) {
            ++position_;
        } else if (!on_ && position_ > 0) {
            --position_;
        }
    }

    hcpp::u8 Switch::position() const noexcept { return position_; }

    void Switch::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Switch::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Switch::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    void Switch::set_focused(bool focused) noexcept { focused_ = focused; }

    bool Switch::focused() const noexcept { return focused_; }

    bool Switch::on_key(char key) noexcept {
        if (key == keys::space) {
            toggle();
            return true;
        }
        return false;
    }

    Rect Switch::focus_ring_rect() const noexcept { return bounds(); }

    Rect Switch::track_rect() const noexcept {
        const Rect b = bounds();
        hcpp::i32 y = b.y + (b.h - track_h) / 2;
        if (y < b.y) {
            y = b.y;
        }
        return {b.x, y, track_w, track_h};
    }

    Size Switch::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const Size text =
                face != nullptr ? text_extent(label_, *face) : Size{};
            desired = {track_w + 8 + text.width,
                       track_h > text.height ? track_h : text.height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Switch::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect track = track_rect();
        const hcpp::i32 radius = track.h / 2;
        canvas.fill_rounded_rect(
            track, radius,
            canvas.color(on_ ? theme.primary : theme.muted));
        canvas.stroke_rounded_rect(track, radius, canvas.color(theme.border));

        const hcpp::i32 knob_r = radius - 2;
        const hcpp::i32 cy = track.y + radius;
        const hcpp::i32 cx_min = track.x + radius;
        const hcpp::i32 cx_max = track.right() - radius;
        const hcpp::i32 cx =
            cx_min +
            (cx_max - cx_min) * static_cast<hcpp::i32>(position_) /
                static_cast<hcpp::i32>(knob_steps);
        canvas.fill_circle(cx, cy, knob_r, canvas.color(theme.primary_fg));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face != nullptr && !label_.empty()) {
            hcpp::i32 offset = (b.h - face->height) / 2;
            if (offset < 0) {
                offset = 0;
            }
            canvas.draw_text(track.right() + 8, b.y + offset + face->baseline,
                             label_, canvas.color(theme.fg), *face);
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

}  // namespace hype::ui
