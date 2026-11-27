#include "kernel/ui/widgets/marquee.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 gap_px = 32;

    }  // namespace

    Marquee::Marquee(hcpp::StringView text) noexcept : text_(text) {}

    void Marquee::set_text(hcpp::StringView text) noexcept { text_ = text; }

    hcpp::StringView Marquee::text() const noexcept { return text_; }

    void Marquee::set_speed(hcpp::u32 pixels_per_frame) noexcept {
        speed_ = pixels_per_frame != 0 ? pixels_per_frame : 1;
    }

    hcpp::u32 Marquee::speed() const noexcept { return speed_; }

    void Marquee::set_offset(hcpp::u32 offset) noexcept { offset_ = offset; }

    hcpp::u32 Marquee::offset() const noexcept { return offset_; }

    bool Marquee::tick(hcpp::u32 frame) noexcept {
        const hcpp::u32 next = frame * speed_;
        if (next == offset_) {
            return false;
        }
        offset_ = next;
        return true;
    }

    void Marquee::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Marquee::font() const noexcept {
        return font_;
    }

    const hcpp::fonts::FontFace* Marquee::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 Marquee::text_width() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || text_.empty()) {
            return 0;
        }
        return text_extent(text_, *face).width;
    }

    hcpp::i32 Marquee::cycle() const noexcept {
        const hcpp::i32 width = text_width();
        return width > 0 ? width + gap_px : 0;
    }

    hcpp::i32 Marquee::text_x() const noexcept {
        const Rect box = bounds();
        const hcpp::i32 length = cycle();
        if (box.empty() || length <= 0) {
            return box.x;
        }
        const hcpp::i32 shift =
            static_cast<hcpp::i32>(offset_ % static_cast<hcpp::u32>(length));
        return box.x - shift;
    }

    Size Marquee::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const hcpp::i32 line = face != nullptr ? face->height : 16;
            const hcpp::i32 width = text_width();
            desired = {width > 0 ? width : 96, line};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Marquee::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 length = cycle();
        if (box.empty() || face == nullptr || length <= 0) {
            return;
        }
        const Theme& theme = theme_active();
        hcpp::i32 offset = (box.h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = box.y + offset + face->baseline;
        const hcpp::i32 start = text_x();
        const Rect saved = canvas.clip();
        canvas.set_clip(box);
        const hcpp::u32 ink = canvas.color(theme.fg);
        canvas.draw_text(start, baseline_y, text_, ink, *face);
        canvas.draw_text(start + length, baseline_y, text_, ink, *face);
        canvas.set_clip(saved);
    }

}  // namespace hype::ui
