#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    class Canvas {
    public:
        explicit Canvas(hcpp::graphics::Framebuffer& target) noexcept;

        hcpp::graphics::Framebuffer& target() const noexcept;
        hcpp::u32 color(hcpp::u32 rgb) const noexcept;

        void set_clip(Rect clip) noexcept;
        void reset_clip() noexcept;
        Rect clip() const noexcept;

        void draw_pixel(hcpp::i32 x, hcpp::i32 y, hcpp::u32 color) noexcept;
        void fill_rect(Rect rect, hcpp::u32 color) noexcept;
        void stroke_rect(Rect rect, hcpp::u32 color, hcpp::u32 thickness = 1) noexcept;
        void draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text, hcpp::u32 color,
                       const hcpp::fonts::FontFace& font) noexcept;
        void draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text,
                       hcpp::u32 color) noexcept;

    private:
        hcpp::graphics::Framebuffer& target_;
        Rect clip_;
    };

}  // namespace hype::ui
