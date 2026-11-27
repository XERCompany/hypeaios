#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/overlays/toast.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class OverlayHost {
    public:
        static constexpr hcpp::u8 scrim_alpha = 140;

        bool open(Widget* modal) noexcept;
        void close() noexcept;
        Widget* modal() const noexcept;
        bool is_open() const noexcept;

        ToastHost& toasts() noexcept;
        const ToastHost& toasts() const noexcept;

        bool handle_key(char ascii) noexcept;
        void paint_overlays(Canvas& canvas, const Rect& screen) noexcept;

    private:
        Widget* modal_ = nullptr;
        ToastHost toasts_;
    };

}  // namespace hype::ui
