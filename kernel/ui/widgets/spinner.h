#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Spinner : public Widget, public Animatable {
    public:
        static constexpr hcpp::u8 phase_count = 8;

        Spinner() noexcept = default;
        explicit Spinner(Size size) noexcept;

        void set_phase(hcpp::u8 phase) noexcept;
        hcpp::u8 phase() const noexcept;

        bool tick(hcpp::u32 frame) noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::u8 phase_ = 0;
    };

}  // namespace hype::ui
