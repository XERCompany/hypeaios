#include "kernel/ui/widgets/badge.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    Badge::Badge(hcpp::StringView text) noexcept : text_(text) {}

    void Badge::set_text(hcpp::StringView text) noexcept { text_ = text; }

    hcpp::StringView Badge::text() const noexcept { return text_; }

    void Badge::set_variant(BadgeVariant variant) noexcept { variant_ = variant; }

    BadgeVariant Badge::variant() const noexcept { return variant_; }

    void Badge::set_padding(Insets padding) noexcept { padding_ = padding; }

    Insets Badge::padding() const noexcept { return padding_; }

    void Badge::set_font(const hcpp::fonts::FontFace* font) noexcept { font_ = font; }

    const hcpp::fonts::FontFace* Badge::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Badge::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Size Badge::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            const Size text = face != nullptr ? text_extent(text_, *face) : Size{};
            desired = {text.width + padding_.horizontal(),
                       text.height + padding_.vertical()};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Badge::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_lg);
        hcpp::u32 text_rgb = theme.fg;
        switch (variant_) {
            case BadgeVariant::Solid:
                canvas.fill_rounded_rect(box, radius, canvas.color(theme.primary));
                text_rgb = theme.primary_fg;
                break;
            case BadgeVariant::Destructive:
                canvas.fill_rounded_rect(box, radius,
                                         canvas.color(theme.destructive));
                text_rgb = theme.primary_fg;
                break;
            case BadgeVariant::Secondary:
                canvas.fill_rounded_rect(box, radius,
                                         canvas.color(theme.secondary));
                text_rgb = theme.fg;
                break;
            case BadgeVariant::Outline:
            default:
                break;
        }
        canvas.stroke_rounded_rect(box, radius, canvas.color(theme.border));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr || text_.empty()) {
            return;
        }
        const Size text = text_extent(text_, *face);
        hcpp::i32 text_x = box.x + (box.w - text.width) / 2;
        if (text_x < box.x) {
            text_x = box.x;
        }
        hcpp::i32 offset = (box.h - face->height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = box.y + offset + face->baseline;
        canvas.draw_text(text_x, baseline_y, text_, canvas.color(text_rgb), *face);
    }

}  // namespace hype::ui
