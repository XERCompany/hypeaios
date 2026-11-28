#include "kernel/ui/widgets/progress.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    Progress::Progress(hcpp::u8 value) noexcept { set_progress(value); }

    void Progress::set_progress(hcpp::u8 value) noexcept {
        value_ = value > 100 ? 100 : value;
    }

    hcpp::u8 Progress::progress() const noexcept { return value_; }

    void Progress::set_indeterminate(bool indeterminate) noexcept {
        indeterminate_ = indeterminate;
        phase_ = 0;
    }

    bool Progress::indeterminate() const noexcept { return indeterminate_; }

    hcpp::u8 Progress::phase() const noexcept { return phase_; }

    bool Progress::tick(hcpp::u32) noexcept {
        if (!indeterminate_) {
            return false;
        }
        phase_ = static_cast<hcpp::u8>((phase_ + 1) % sweep_period);
        return true;
    }

    void Progress::set_label(hcpp::StringView label) noexcept { label_ = label; }

    hcpp::StringView Progress::label() const noexcept { return label_; }

    void Progress::set_track_height(hcpp::i32 height) noexcept {
        track_height_ = height;
    }

    hcpp::i32 Progress::track_height() const noexcept {
        return track_height_ > 0 ? track_height_ : default_track_height;
    }

    void Progress::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Progress::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Progress::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Size Progress::measure(Size available) noexcept {
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = face != nullptr ? face->height : 16;
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_sm);

        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            hcpp::i32 width = default_width;
            if (face != nullptr && !label_.empty()) {
                const Size text = text_extent(label_, *face);
                if (text.width > width) {
                    width = text.width;
                }
            }
            hcpp::i32 height = track_height();
            if (!label_.empty()) {
                height += line + gap;
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

    void Progress::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::i32 track_h = track_height();
        const Rect track{box.x, box.bottom() - track_h, box.w, track_h};
        if (track.empty()) {
            return;
        }
        const hcpp::i32 radius = track_h / 2;
        canvas.fill_rounded_rect(track, radius, canvas.color(theme.muted));
        if (indeterminate_) {
            // Sweep: a 32px accent segment crossing the track on a
            // sweep_period-frame loop, clipped to the track rect.
            const hcpp::i32 seg = track.w < 32 ? track.w : 32;
            const hcpp::i32 travel = track.w + seg;
            const hcpp::i32 x = track.x - seg +
                travel * static_cast<hcpp::i32>(phase_) /
                    static_cast<hcpp::i32>(sweep_period);
            const Rect clipped =
                Rect{x, track.y, seg, track.h}.intersection(track);
            if (!clipped.empty()) {
                canvas.fill_rounded_rect(clipped, radius,
                                         canvas.color(theme.primary));
            }
        } else {
            const hcpp::i32 fill_w = track.w * value_ / 100;
            if (fill_w > 0) {
                canvas.fill_rounded_rect(
                    {track.x, track.y, fill_w, track.h}, radius,
                    canvas.color(theme.primary));
            }
        }

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || label_.empty()) {
            return;
        }
        canvas.draw_text(box.x, box.y + face->baseline, label_,
                         canvas.color(theme.fg), *face);
    }

}  // namespace hype::ui
