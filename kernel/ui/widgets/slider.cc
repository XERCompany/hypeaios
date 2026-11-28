#include "kernel/ui/widgets/slider.h"

#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::size_t format_i32(char* out, hcpp::size_t capacity,
                                hcpp::i32 value) noexcept {
            if (capacity == 0) {
                return 0;
            }
            hcpp::size_t n = 0;
            hcpp::u32 mag;
            if (value < 0) {
                out[n++] = '-';
                mag = static_cast<hcpp::u32>(-(value + 1)) + 1;
            } else {
                mag = static_cast<hcpp::u32>(value);
            }
            char digits[10];
            hcpp::size_t count = 0;
            do {
                digits[count++] = static_cast<char>('0' + mag % 10);
                mag /= 10;
            } while (mag != 0 && count < sizeof(digits));
            while (count != 0 && n < capacity) {
                out[n++] = digits[--count];
            }
            if (n < capacity) {
                out[n] = '\0';
            }
            return n;
        }

    }  // namespace

    Slider::Slider(hcpp::i32 vmin, hcpp::i32 vmax, hcpp::i32 value) noexcept {
        set_range(vmin, vmax);
        set_value(value);
    }

    void Slider::set_range(hcpp::i32 vmin, hcpp::i32 vmax) noexcept {
        vmin_ = vmin;
        vmax_ = vmax > vmin ? vmax : vmin + 1;
        set_value(value_);
    }

    hcpp::i32 Slider::vmin() const noexcept { return vmin_; }

    hcpp::i32 Slider::vmax() const noexcept { return vmax_; }

    void Slider::set_step(hcpp::i32 step) noexcept {
        step_ = step > 0 ? step : 1;
        set_value(value_);
    }

    hcpp::i32 Slider::step() const noexcept { return step_; }

    void Slider::set_value(hcpp::i32 value) noexcept { apply_value(value); }

    hcpp::i32 Slider::value() const noexcept { return value_; }

    void Slider::set_enabled(bool enabled) noexcept {
        enabled_ = enabled;
        if (!enabled) {
            hovered_ = false;
            grabbed_ = false;
        }
    }

    bool Slider::enabled() const noexcept { return enabled_; }

    void Slider::set_invalid(bool invalid) noexcept { invalid_ = invalid; }

    bool Slider::invalid() const noexcept { return invalid_; }

    void Slider::set_show_value(bool show) noexcept { show_value_ = show; }

    bool Slider::show_value() const noexcept { return show_value_; }

    bool Slider::hovered() const noexcept { return hovered_; }

    bool Slider::grabbed() const noexcept { return grabbed_; }

    void Slider::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Slider::font() const noexcept {
        return font_;
    }

    void Slider::set_on_change(SliderCallback callback,
                               void* context) noexcept {
        on_change_ = callback;
        change_ctx_ = context;
    }

    void Slider::set_on_commit(SliderCallback callback,
                               void* context) noexcept {
        on_commit_ = callback;
        commit_ctx_ = context;
    }

    bool Slider::hit_test(Point point) const noexcept {
        return bounds().contains(point);
    }

    bool Slider::handle_mouse(Point point, SliderMouse event) noexcept {
        if (!enabled_) {
            return false;
        }
        if (event == SliderMouse::Down) {
            if (!hit_test(point)) {
                return false;
            }
            hovered_ = true;
            grabbed_ = true;
            apply_value(value_at(point.x));
            return true;
        }
        if (event == SliderMouse::Move) {
            if (grabbed_) {
                const hcpp::i32 before = value_;
                apply_value(value_at(point.x));
                return value_ != before;
            }
            const bool hover = hit_test(point);
            if (hover != hovered_) {
                hovered_ = hover;
                return true;
            }
            return false;
        }
        // Up
        if (grabbed_) {
            grabbed_ = false;
            apply_value(value_at(point.x));
            commit();
            hovered_ = hit_test(point);
            return true;
        }
        const bool hover = hit_test(point);
        if (hover != hovered_) {
            hovered_ = hover;
            return true;
        }
        return false;
    }

    hcpp::i32 Slider::value_at(hcpp::i32 x) const noexcept {
        const Rect track = track_rect();
        if (track.w <= 0) {
            return vmin_;
        }
        const hcpp::i32 span = vmax_ - vmin_;
        hcpp::i64 num = static_cast<hcpp::i64>(x - track.x) * span;
        hcpp::i32 v =
            vmin_ + static_cast<hcpp::i32>((num + track.w / 2) / track.w);
        return clamp_step(v);
    }

    void Slider::set_focused(bool focused) noexcept { focused_ = focused; }

    bool Slider::focused() const noexcept { return focused_; }

    bool Slider::on_key(char key) noexcept {
        if (!enabled_) {
            return false;
        }
        hcpp::i32 delta = 0;
        if (key == keys::left) {
            delta = -step_;
        } else if (key == keys::right) {
            delta = step_;
        } else if (key == keys::down) {
            delta = -10 * step_;
        } else if (key == keys::up) {
            delta = 10 * step_;
        } else {
            return false;
        }
        const hcpp::i32 before = value_;
        apply_value(value_ + delta);
        if (value_ != before) {
            commit();
            return true;
        }
        return true;
    }

    Rect Slider::focus_ring_rect() const noexcept { return thumb_rect(); }

    Size Slider::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            desired = {160, default_height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Slider::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect track = track_rect();
        const Rect thumb = thumb_rect();
        const hcpp::i32 half = thumb_diameter / 2;

        const hcpp::u32 range_rgb =
            invalid_ ? theme.destructive
                     : (enabled_ ? theme.accent : theme.muted);
        canvas.fill_rounded_rect(track, track.h / 2,
                                 canvas.color(theme.muted));
        const hcpp::i32 fill_w = thumb.x + half - track.x;
        if (fill_w > 0) {
            canvas.fill_rounded_rect({track.x, track.y, fill_w, track.h},
                                     track.h / 2, canvas.color(range_rgb));
        }

        // Thumb: soft shadow step while hovered/grabbed (elevated), then
        // surface circle + hairline, then focus ring outside.
        const hcpp::u32 thumb_fill =
            enabled_ ? theme.card : theme.muted;
        if (enabled_ && (grabbed_ || hovered_)) {
            rui_shadow(canvas, thumb, half, grabbed_ ? 8 : 5, 90);
        }
        canvas.fill_circle(thumb.x + half, thumb.y + half, half,
                           canvas.color(thumb_fill));
        canvas.stroke_circle(thumb.x + half, thumb.y + half, half - 1,
                             canvas.color(enabled_ ? theme.border
                                                   : theme.muted_fg));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (show_value_ && face != nullptr) {
            char text[12];
            const hcpp::size_t len =
                format_i32(text, sizeof(text) - 1, value_);
            const hcpp::StringView sv{text, len};
            const Size extent = text_extent(sv, *face);
            const hcpp::i32 tx = thumb.x + half - extent.width / 2;
            const hcpp::i32 ty = track.y - extent.height - 2;
            if (ty >= b.y) {
                canvas.draw_text(tx < b.x ? b.x : tx, ty + face->baseline,
                                 sv,
                                 canvas.color(enabled_ ? theme.fg
                                                       : theme.muted_fg),
                                 *face);
            }
        }

        if (focused_) {
            draw_focus_ring(canvas, thumb);
        }
    }

    const hcpp::fonts::FontFace* Slider::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Rect Slider::track_rect() const noexcept {
        const Rect b = bounds();
        const hcpp::i32 half = thumb_diameter / 2;
        hcpp::i32 y = b.y + (b.h - track_height) / 2;
        if (show_value_ && b.h > thumb_diameter + 8) {
            y = b.bottom() - half - track_height / 2;
        }
        if (y < b.y) {
            y = b.y;
        }
        hcpp::i32 w = b.w - thumb_diameter;
        if (w < 1) {
            w = 1;
        }
        return {b.x + half, y, w, track_height};
    }

    Rect Slider::thumb_rect() const noexcept {
        const Rect track = track_rect();
        const hcpp::i32 span = vmax_ - vmin_;
        const hcpp::i32 frac = span > 0 ? value_ - vmin_ : 0;
        const hcpp::i32 cx =
            track.x + static_cast<hcpp::i32>(
                          static_cast<hcpp::i64>(frac) * track.w / span);
        const hcpp::i32 cy = track.y + track.h / 2;
        return {cx - thumb_diameter / 2, cy - thumb_diameter / 2,
                thumb_diameter, thumb_diameter};
    }

    hcpp::i32 Slider::clamp_step(hcpp::i32 value) const noexcept {
        if (value < vmin_) {
            value = vmin_;
        }
        if (value > vmax_) {
            value = vmax_;
        }
        const hcpp::i32 rel = value - vmin_;
        const hcpp::i32 rem = rel % step_;
        hcpp::i32 snapped = value - rem;
        if (rem * 2 >= step_ && snapped + step_ <= vmax_) {
            snapped += step_;
        }
        if (snapped < vmin_) {
            snapped = vmin_;
        }
        if (snapped > vmax_) {
            snapped = vmax_;
        }
        return snapped;
    }

    void Slider::apply_value(hcpp::i32 value) noexcept {
        const hcpp::i32 v = clamp_step(value);
        if (v == value_) {
            return;
        }
        value_ = v;
        if (on_change_ != nullptr) {
            on_change_(*this, value_, change_ctx_);
        }
    }

    void Slider::commit() noexcept {
        if (on_commit_ != nullptr) {
            on_commit_(*this, value_, commit_ctx_);
        }
    }

}  // namespace hype::ui
