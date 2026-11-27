#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Skeleton : public Widget, public Animatable {
    public:
        Skeleton() noexcept = default;
        explicit Skeleton(Size size) noexcept;

        void set_phase(hcpp::u8 phase) noexcept;
        hcpp::u8 phase() const noexcept;

        bool tick(hcpp::u32 frame) noexcept override;

        void set_radius(hcpp::i32 radius) noexcept;
        hcpp::i32 radius() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::u8 phase_ = 0;
        hcpp::i32 radius_ = 0;
    };

}  // namespace hype::ui
