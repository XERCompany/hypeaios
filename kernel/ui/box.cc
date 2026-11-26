#include "kernel/ui/box.h"

namespace hype::ui {

    Box::Box(Size size) noexcept { set_preferred_size(size); }

    Box::Box(Size size, hcpp::u32 fill_rgb) noexcept {
        set_preferred_size(size);
        set_fill(fill_rgb);
    }

    void Box::set_fill(hcpp::u32 fill_rgb) noexcept {
        fill_rgb_ = fill_rgb;
        filled_ = true;
    }

    void Box::clear_fill() noexcept { filled_ = false; }

    bool Box::has_fill() const noexcept { return filled_; }

    hcpp::u32 Box::fill_rgb() const noexcept { return fill_rgb_; }

    void Box::paint(Canvas& canvas) noexcept {
        if (filled_ && !bounds().empty()) {
            canvas.fill_rect(bounds(), canvas.color(fill_rgb_));
        }
    }

}  // namespace hype::ui
