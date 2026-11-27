#include "kernel/ui/widgets/skeleton.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::u32 blend_rgb(hcpp::u32 from, hcpp::u32 to, hcpp::u32 amount) noexcept {
            const hcpp::u32 inverse = 255 - amount;
            const hcpp::u32 red = (((from >> 16) & 0xFF) * inverse +
                                   ((to >> 16) & 0xFF) * amount) / 255;
            const hcpp::u32 green = (((from >> 8) & 0xFF) * inverse +
                                     ((to >> 8) & 0xFF) * amount) / 255;
            const hcpp::u32 blue = ((from & 0xFF) * inverse +
                                    (to & 0xFF) * amount) / 255;
            return (red << 16) | (green << 8) | blue;
        }

    }  // namespace

    Skeleton::Skeleton(Size size) noexcept { set_preferred_size(size); }

    void Skeleton::set_phase(hcpp::u8 phase) noexcept { phase_ = phase; }

    hcpp::u8 Skeleton::phase() const noexcept { return phase_; }

    bool Skeleton::tick(hcpp::u32 frame) noexcept {
        const hcpp::u8 next = static_cast<hcpp::u8>((frame * 8) & 0xFF);
        if (next == phase_) {
            return false;
        }
        phase_ = next;
        return true;
    }

    void Skeleton::set_radius(hcpp::i32 radius) noexcept { radius_ = radius; }

    hcpp::i32 Skeleton::radius() const noexcept {
        if (radius_ > 0) {
            return radius_;
        }
        return static_cast<hcpp::i32>(theme_active().radius_md);
    }

    Size Skeleton::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0) {
            desired.width = 96;
        }
        if (desired.height <= 0) {
            desired.height = 16;
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Skeleton::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::u32 tri =
            phase_ < 128 ? phase_ : static_cast<hcpp::u32>(255 - phase_);
        const hcpp::u32 shade = blend_rgb(theme.muted, theme.border, 64 + tri);
        canvas.fill_rounded_rect(box, radius(), canvas.color(shade));
    }

}  // namespace hype::ui
