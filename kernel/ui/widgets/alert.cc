#include "kernel/ui/widgets/alert.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 bar_width = 4;

        hcpp::u32 tone_rgb(AlertTone tone, const Theme& theme) noexcept {
            switch (tone) {
                case AlertTone::Success:
                    return theme.success;
                case AlertTone::Warning:
                    return theme.warning;
                case AlertTone::Destructive:
                    return theme.destructive;
                case AlertTone::Info:
                default:
                    return theme.primary;
            }
        }

    }  // namespace

    Alert::Alert(hcpp::StringView title, hcpp::StringView description) noexcept
        : title_(title), description_(description) {}

    void Alert::set_title(hcpp::StringView title) noexcept { title_ = title; }

    hcpp::StringView Alert::title() const noexcept { return title_; }

    void Alert::set_description(hcpp::StringView description) noexcept {
        description_ = description;
    }

    hcpp::StringView Alert::description() const noexcept { return description_; }

    void Alert::set_tone(AlertTone tone) noexcept { tone_ = tone; }

    AlertTone Alert::tone() const noexcept { return tone_; }

    void Alert::set_font(const hcpp::fonts::FontFace* font) noexcept { font_ = font; }

    const hcpp::fonts::FontFace* Alert::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Alert::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Size Alert::measure(Size available) noexcept {
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = face != nullptr ? face->height : 16;
        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_sm);

        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            hcpp::i32 text_w = 0;
            if (face != nullptr) {
                const Size title_extent = text_extent(title_, *face);
                const Size desc_extent = text_extent(description_, *face);
                text_w = title_extent.width > desc_extent.width
                             ? title_extent.width
                             : desc_extent.width;
            }
            hcpp::i32 height = 2 * pad + line;
            if (!description_.empty()) {
                height += gap + line;
            }
            desired = {1 + bar_width + pad + text_w + pad + 1, height};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Alert::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::u32 accent = canvas.color(tone_rgb(tone_, theme));
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_lg);
        canvas.fill_rounded_rect(box, radius, canvas.color(theme.card));
        canvas.stroke_rounded_rect(box, radius, canvas.color(theme.border));

        hcpp::i32 bar_h = box.h - 2 * radius;
        if (bar_h < 2) {
            bar_h = box.h - 4;
        }
        const hcpp::i32 bar_y = box.y + (box.h - bar_h) / 2;
        canvas.fill_rect({box.x + 1, bar_y, bar_width, bar_h}, accent);

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr) {
            return;
        }
        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_sm);
        const hcpp::i32 text_x = box.x + 1 + bar_width + pad;
        hcpp::i32 baseline = box.y + pad + face->baseline;
        if (!title_.empty()) {
            canvas.draw_text(text_x, baseline, title_, accent, *face);
            baseline += face->height + gap;
        }
        if (!description_.empty()) {
            canvas.draw_text(text_x, baseline, description_,
                             canvas.color(theme.muted_fg), *face);
        }
    }

}  // namespace hype::ui
