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

        bool show(hcpp::StringView message, ToastKind kind = ToastKind::Info,
                  hcpp::StringView title = {}) noexcept;
        void clear() noexcept;
        hcpp::size_t count() const noexcept;

        void paint(Canvas& canvas, const Rect& screen) noexcept;

    private:
        struct Toast {
            hcpp::StringView title;
            hcpp::StringView message;
            ToastKind kind = ToastKind::Info;
        };

        Toast toasts_[max_toasts];
        hcpp::size_t count_ = 0;
    };

}  // namespace hype::ui
