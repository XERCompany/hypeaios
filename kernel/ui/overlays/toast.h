#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    enum class ToastKind : hcpp::u8 { Info, Success, Warning, Error };

    class ToastHost {
    public:
        static constexpr hcpp::size_t max_toasts = 8;
        static constexpr hcpp::u32 default_duration_ticks = 300;

        bool show(hcpp::StringView message, ToastKind kind = ToastKind::Info,
                  hcpp::StringView title = {},
                  hcpp::u32 duration_ticks = default_duration_ticks) noexcept;
        bool dismiss(hcpp::size_t index) noexcept;
        void clear() noexcept;
        hcpp::size_t count() const noexcept;

        void paint(Canvas& canvas, const Rect& screen) noexcept;

    private:
        struct Toast {
            hcpp::StringView title;
            hcpp::StringView message;
            ToastKind kind = ToastKind::Info;
            hcpp::u32 remaining = 0;
            // Paint-frame age driving the slide-in from the screen edge.
            hcpp::u32 age = 0;
        };

        Toast toasts_[max_toasts];
        hcpp::size_t count_ = 0;
    };

}  // namespace hype::ui
