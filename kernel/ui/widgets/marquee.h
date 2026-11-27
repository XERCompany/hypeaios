#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Marquee : public Widget, public Animatable {
    public:
        Marquee() noexcept = default;
        explicit Marquee(hcpp::StringView text) noexcept;

        void set_text(hcpp::StringView text) noexcept;
        hcpp::StringView text() const noexcept;

        void set_speed(hcpp::u32 pixels_per_frame) noexcept;
        hcpp::u32 speed() const noexcept;

        void set_offset(hcpp::u32 offset) noexcept;
        hcpp::u32 offset() const noexcept;

        bool tick(hcpp::u32 frame) noexcept override;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        hcpp::i32 text_width() const noexcept;
        hcpp::i32 cycle() const noexcept;
        hcpp::i32 text_x() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::StringView text_;
        hcpp::u32 speed_ = 2;
        hcpp::u32 offset_ = 0;
    };

}  // namespace hype::ui
