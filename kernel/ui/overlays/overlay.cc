#include "kernel/ui/overlays/overlay.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr char escape_ascii = 27;

    }  // namespace

    bool OverlayHost::open(Widget* modal) noexcept {
        if (modal == nullptr) {
            return false;
        }
        modal_ = modal;
        modal_->set_visible(true);
        return true;
    }

    void OverlayHost::close() noexcept { modal_ = nullptr; }

    Widget* OverlayHost::modal() const noexcept { return modal_; }

    bool OverlayHost::is_open() const noexcept { return modal_ != nullptr; }

    ToastHost& OverlayHost::toasts() noexcept { return toasts_; }

    const ToastHost& OverlayHost::toasts() const noexcept { return toasts_; }

    bool OverlayHost::handle_key(char ascii) noexcept {
        if (modal_ == nullptr || !modal_->is_visible()) {
            return false;
        }
        if (ascii == escape_ascii) {
            modal_->handle_key(ascii);
            close();
            return true;
        }
        modal_->handle_key(ascii);
        return true;
    }

    void OverlayHost::paint_overlays(Canvas& canvas, const Rect& screen) noexcept {
        if (modal_ != nullptr && modal_->is_visible()) {
            const Theme& theme = theme_active();
            // Modal scrim (issue #177): #000 @55% dark / @40% light.
            canvas.fill_rect_alpha(screen, canvas.color(theme.scrim),
                                   theme.scrim_alpha);

            const Size desired = modal_->measure({screen.w, screen.h});
            hcpp::i32 width = desired.width > 0 ? desired.width : screen.w / 2;
            hcpp::i32 height = desired.height > 0 ? desired.height : screen.h / 3;
            if (width > screen.w) {
                width = screen.w;
            }
            if (height > screen.h) {
                height = screen.h;
            }
            const Rect modal_rect{screen.x + (screen.w - width) / 2,
                                  screen.y + (screen.h - height) / 2, width,
                                  height};
            modal_->layout_tree(modal_rect);
            modal_->paint_tree(canvas);
        }
        toasts_.paint(canvas, screen);
    }

}  // namespace hype::ui
