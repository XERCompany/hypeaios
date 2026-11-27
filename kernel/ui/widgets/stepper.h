#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Stepper : public Widget {
    public:
        static constexpr hcpp::size_t max_steps = 6;
        static constexpr hcpp::size_t label_capacity = 16;

        Stepper() noexcept = default;

        bool set_step_count(hcpp::size_t count) noexcept;
        hcpp::size_t step_count() const noexcept;

        bool set_step_label(hcpp::size_t step, hcpp::StringView label) noexcept;
        hcpp::StringView step_label(hcpp::size_t step) const noexcept;

        void set_current(hcpp::i32 step) noexcept;
        hcpp::i32 current() const noexcept;

        void set_circle_radius(hcpp::i32 radius) noexcept;
        hcpp::i32 circle_radius() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Point step_center(hcpp::size_t step) const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        char labels_[max_steps][label_capacity] = {};
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::size_t step_count_ = 0;
        hcpp::i32 current_ = 0;
        hcpp::i32 radius_ = 8;
    };

}  // namespace hype::ui
