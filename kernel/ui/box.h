#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Box : public Widget {
    public:
        Box() noexcept = default;
        explicit Box(Size size) noexcept;
        Box(Size size, hcpp::u32 fill_rgb) noexcept;

        void set_fill(hcpp::u32 fill_rgb) noexcept;
        void clear_fill() noexcept;
        bool has_fill() const noexcept;
        hcpp::u32 fill_rgb() const noexcept;

        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::u32 fill_rgb_ = 0;
        bool filled_ = false;
    };

}  // namespace hype::ui
