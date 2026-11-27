#include "kernel/ui/widgets/stepper.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    bool Stepper::set_step_count(hcpp::size_t count) noexcept {
        if (count > max_steps) {
            return false;
        }
        step_count_ = count;
        if (current_ >= static_cast<hcpp::i32>(count)) {
            current_ = count > 0 ? static_cast<hcpp::i32>(count) - 1 : 0;
        }
        return true;
    }

    hcpp::size_t Stepper::step_count() const noexcept { return step_count_; }

    bool Stepper::set_step_label(hcpp::size_t step,
                                 hcpp::StringView label) noexcept {
        if (step >= max_steps) {
            return false;
        }
        hcpp::size_t count = label.size();
        if (count > label_capacity - 1) {
            count = label_capacity - 1;
        }
        for (hcpp::size_t i = 0; i < count; ++i) {
            labels_[step][i] = label[i];
        }
        labels_[step][count] = '\0';
        return true;
    }

    hcpp::StringView Stepper::step_label(hcpp::size_t step) const noexcept {
        if (step >= max_steps) {
            return {};
        }
        return hcpp::StringView(labels_[step]);
    }

    void Stepper::set_current(hcpp::i32 step) noexcept {
        if (step < 0) {
            step = 0;
        }
        if (step >= static_cast<hcpp::i32>(step_count_)) {
            step = step_count_ > 0 ? static_cast<hcpp::i32>(step_count_) - 1 : 0;
        }
        current_ = step;
    }

    hcpp::i32 Stepper::current() const noexcept { return current_; }

    void Stepper::set_circle_radius(hcpp::i32 radius) noexcept {
        radius_ = radius > 2 ? radius : 2;
    }

    hcpp::i32 Stepper::circle_radius() const noexcept { return radius_; }

    void Stepper::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Stepper::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Stepper::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Point Stepper::step_center(hcpp::size_t step) const noexcept {
        const Rect box = bounds();
        if (step_count_ == 0 || step >= step_count_) {
            return {};
        }
        const hcpp::i32 slot = box.w / static_cast<hcpp::i32>(step_count_);
        return {box.x + slot * static_cast<hcpp::i32>(step) + slot / 2,
                box.y + radius_ + 2};
    }

    Size Stepper::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            desired = {180, 2 * radius_ + 4 + line + 6};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Stepper::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty() || step_count_ == 0) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::u32 success = canvas.color(theme.success);
        const hcpp::u32 primary = canvas.color(theme.primary);
        const hcpp::u32 muted = canvas.color(theme.muted);
        const hcpp::i32 cy = box.y + radius_ + 2;

        for (hcpp::size_t i = 1; i < step_count_; ++i) {
            const Point prev = step_center(i - 1);
            const Point here = step_center(i);
            const hcpp::u32 rule =
                static_cast<hcpp::i32>(i) <= current_ ? success : muted;
            canvas.fill_rect(
                {prev.x + radius_ + 1, cy, here.x - prev.x - 2 * radius_ - 2, 1},
                rule);
        }
        for (hcpp::size_t i = 0; i < step_count_; ++i) {
            const Point center = step_center(i);
            const hcpp::i32 index = static_cast<hcpp::i32>(i);
            if (index < current_) {
                canvas.fill_circle(center.x, cy, radius_, success);
                canvas.draw_line(center.x - radius_ / 2, cy,
                                 center.x - 1, cy + radius_ / 2,
                                 canvas.color(theme.primary_fg));
                canvas.draw_line(center.x - 1, cy + radius_ / 2,
                                 center.x + radius_ / 2, cy - radius_ / 2,
                                 canvas.color(theme.primary_fg));
            } else if (index == current_) {
                canvas.fill_circle(center.x, cy, radius_, primary);
            } else {
                canvas.stroke_circle(center.x, cy, radius_, muted);
            }
            if (face != nullptr && index >= current_) {
                char digit = static_cast<char>('1' + index);
                if (index >= 9) {
                    digit = '*';
                }
                const hcpp::u32 digit_rgb =
                    index == current_ ? canvas.color(theme.primary_fg)
                                      : canvas.color(theme.muted_fg);
                canvas.draw_text(center.x - face->advance / 2,
                                 cy - face->height / 2 + face->baseline,
                                 hcpp::StringView(&digit, 1), digit_rgb, *face);
            }
            if (face != nullptr) {
                const hcpp::StringView label(labels_[i]);
                if (!label.empty()) {
                    hcpp::i32 lx =
                        center.x - text_extent(label, *face).width / 2;
                    if (lx < box.x) {
                        lx = box.x;
                    }
                    const hcpp::u32 label_rgb =
                        index == current_ ? canvas.color(theme.fg)
                                          : canvas.color(theme.muted_fg);
                    canvas.draw_text(lx,
                                     cy + radius_ + 4 + face->baseline, label,
                                     label_rgb, *face);
                }
            }
        }
    }

}  // namespace hype::ui
