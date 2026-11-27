#include "kernel/ui/widgets/separator.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    Separator::Separator(SeparatorOrientation orientation) noexcept
        : orientation_(orientation) {}

    void Separator::set_orientation(SeparatorOrientation orientation) noexcept {
        orientation_ = orientation;
    }

    SeparatorOrientation Separator::orientation() const noexcept {
        return orientation_;
    }

    void Separator::set_thickness(hcpp::i32 thickness) noexcept {
        if (thickness < 1) {
            thickness = 1;
        }
        if (thickness > 2) {
            thickness = 2;
        }
        thickness_ = thickness;
    }

    hcpp::i32 Separator::thickness() const noexcept { return thickness_; }

    Size Separator::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            if (orientation_ == SeparatorOrientation::Horizontal) {
                desired = {1, thickness_};
            } else {
                desired = {thickness_, 1};
            }
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Separator::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        canvas.fill_rect(box, canvas.color(theme_active().border));
    }

}  // namespace hype::ui
