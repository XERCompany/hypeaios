#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class NumberTicker : public Widget {
    public:
        NumberTicker() noexcept = default;
        explicit NumberTicker(hcpp::i32 target) noexcept;

        void set_target(hcpp::i32 target) noexcept;
        hcpp::i32 target() const noexcept;

        void set_value(hcpp::i32 value) noexcept;
        hcpp::i32 value() const noexcept;

        void set_step(hcpp::i32 step) noexcept;
        hcpp::i32 step() const noexcept;

        void set_prefix(hcpp::StringView prefix) noexcept;
        void set_suffix(hcpp::StringView suffix) noexcept;

        bool tick() noexcept;
        bool done() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        hcpp::size_t format(char* out, hcpp::size_t capacity) const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::StringView prefix_;
        hcpp::StringView suffix_;
        hcpp::i32 value_ = 0;
        hcpp::i32 target_ = 0;
        hcpp::i32 step_ = 1;
    };

}  // namespace hype::ui
