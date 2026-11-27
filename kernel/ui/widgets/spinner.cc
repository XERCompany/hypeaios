#include "kernel/ui/widgets/spinner.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        const hcpp::i32 spoke_dx[Spinner::phase_count] = {1, 1, 0, -1,
                                                          -1, -1, 0, 1};
        const hcpp::i32 spoke_dy[Spinner::phase_count] = {0, 1, 1, 1,
                                                          0, -1, -1, -1};

    }  // namespace

    Spinner::Spinner(Size size) noexcept { set_preferred_size(size); }

    void Spinner::set_phase(hcpp::u8 phase) noexcept {
        phase_ = static_cast<hcpp::u8>(phase % phase_count);
    }

    hcpp::u8 Spinner::phase() const noexcept { return phase_; }

    bool Spinner::tick(hcpp::u32 frame) noexcept {
        const hcpp::u8 next =
            static_cast<hcpp::u8>(frame % phase_count);
        if (next == phase_) {
            return false;
        }
        phase_ = next;
        return true;
    }

    Size Spinner::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0) {
            desired.width = 16;
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

    void Spinner::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::i32 cx = box.x + box.w / 2;
        const hcpp::i32 cy = box.y + box.h / 2;
        hcpp::i32 radius = (box.w < box.h ? box.w : box.h) / 2 - 1;
        if (radius < 3) {
            radius = 3;
        }
        const hcpp::i32 inner = radius * 2 / 5;
        for (hcpp::u8 i = 0; i < phase_count; ++i) {
            hcpp::u32 rgb = theme.muted;
            if (i == phase_) {
                rgb = theme.primary;
            } else if (i == (phase_ + phase_count - 1) % phase_count) {
                rgb = theme.fg;
            }
            canvas.draw_line(cx + spoke_dx[i] * inner,
                             cy + spoke_dy[i] * inner,
                             cx + spoke_dx[i] * radius,
                             cy + spoke_dy[i] * radius,
                             canvas.color(rgb));
        }
    }

}  // namespace hype::ui
