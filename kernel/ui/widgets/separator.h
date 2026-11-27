#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class SeparatorOrientation : hcpp::u8 { Horizontal, Vertical };

    class Separator : public Widget {
    public:
        Separator() noexcept = default;
        explicit Separator(SeparatorOrientation orientation) noexcept;

        void set_orientation(SeparatorOrientation orientation) noexcept;
        SeparatorOrientation orientation() const noexcept;

        void set_thickness(hcpp::i32 thickness) noexcept;
        hcpp::i32 thickness() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        SeparatorOrientation orientation_ = SeparatorOrientation::Horizontal;
        hcpp::i32 thickness_ = 1;
    };

}  // namespace hype::ui
