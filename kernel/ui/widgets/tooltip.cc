#include "kernel/ui/widgets/tooltip.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 pad_x = 10;
        constexpr hcpp::i32 pad_y = 6;
        constexpr hcpp::i32 line_gap = 2;
        constexpr hcpp::i32 radius = 6;

    }  // namespace

    void Tooltip::arm(Rect target, hcpp::StringView text) noexcept {
        arm(target, text, {});
    }

    void Tooltip::arm(Rect target, hcpp::StringView text,
                      hcpp::StringView title) noexcept {
        if (armed_ && target_ == target && text.data() == text_.data()) {
            return;
        }
        target_ = target;
        text_ = text;
        title_ = title;
        arm_frame_ = anim_ticker().frame();
        armed_for_ = 0;
        armed_ = true;
        visible_ = false;
    }

    void Tooltip::dismiss() noexcept {
        armed_ = false;
        visible_ = false;
        armed_for_ = 0;
    }

    bool Tooltip::armed() const noexcept { return armed_; }

    bool Tooltip::visible() const noexcept { return visible_; }

    hcpp::u32 Tooltip::armed_for() const noexcept { return armed_for_; }

    void Tooltip::set_placement(TooltipPlacement placement) noexcept {
        placement_ = placement;
    }

    TooltipPlacement Tooltip::placement() const noexcept {
        return placement_;
    }

    void Tooltip::set_screen(Rect screen) noexcept { screen_ = screen; }

    Rect Tooltip::screen() const noexcept { return screen_; }

    void Tooltip::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Tooltip::font() const noexcept {
        return font_;
    }

    Rect Tooltip::tip_rect() const noexcept {
        const Size content = content_size();
        Rect tip{0, 0, content.width + pad_x * 2, content.height + pad_y * 2};

        bool below = placement_ == TooltipPlacement::Below;
        if (placement_ == TooltipPlacement::Auto) {
            const hcpp::i32 above_y = target_.y - tip.h - gap;
            below = !screen_.empty() && above_y < screen_.y;
        }
        tip.y = below ? target_.bottom() + gap : target_.y - tip.h - gap;
        tip.x = target_.x + (target_.w - tip.w) / 2;
        if (!screen_.empty()) {
            if (tip.x < screen_.x) {
                tip.x = screen_.x;
            }
            if (tip.right() > screen_.right()) {
                tip.x = screen_.right() - tip.w;
            }
            if (tip.x < screen_.x) {
                tip.x = screen_.x;
            }
        }
        return tip;
    }

    Rect Tooltip::target() const noexcept { return target_; }

    bool Tooltip::tick(hcpp::u32 frame) noexcept {
        if (!armed_ || visible_) {
            return false;
        }
        armed_for_ = frame >= arm_frame_ ? frame - arm_frame_ : 0;
        if (armed_for_ >= delay_frames) {
            visible_ = true;
            return true;
        }
        return false;
    }

    void Tooltip::paint(Canvas& canvas) const noexcept {
        if (!visible_) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect tip = tip_rect();
        if (tip.empty()) {
            return;
        }
        // shadow-lg -> wider blur, then inverted fill: text color as the
        // surface, surface color as the text.
        rui_shadow(canvas, tip, radius, 12, 110);
        canvas.fill_rounded_rect(tip, radius, canvas.color(theme.fg));

        const hcpp::fonts::FontFace* face = resolved_font();
        if (face == nullptr) {
            return;
        }
        hcpp::i32 baseline = tip.y + pad_y + face->baseline;
        if (!title_.empty()) {
            canvas.draw_text(tip.x + pad_x, baseline, title_,
                             canvas.color(theme.bg), *face);
            baseline += face->height + line_gap;
        }
        if (!text_.empty()) {
            canvas.draw_text(tip.x + pad_x, baseline, text_,
                             canvas.color(title_.empty() ? theme.bg
                                                         : theme.muted),
                             *face);
        }
    }

    const hcpp::fonts::FontFace* Tooltip::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    Size Tooltip::content_size() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = face != nullptr ? face->height : 16;
        hcpp::i32 widest = 0;
        if (face != nullptr) {
            const Size body = text_extent(text_, *face);
            const Size head = text_extent(title_, *face);
            widest = body.width > head.width ? body.width : head.width;
        }
        hcpp::i32 height = line;
        if (!title_.empty() && !text_.empty()) {
            height = line * 2 + line_gap;
        }
        return {widest, height};
    }

}  // namespace hype::ui
