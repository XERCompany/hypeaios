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
        void draw_line(hcpp::i32 x0, hcpp::i32 y0, hcpp::i32 x1, hcpp::i32 y1,
                       hcpp::u32 color) noexcept;
        void fill_rect(Rect rect, hcpp::u32 color) noexcept;
        void fill_rect_alpha(Rect rect, hcpp::u32 color, hcpp::u8 alpha) noexcept;
        void fill_rounded_rect(Rect rect, hcpp::i32 radius, hcpp::u32 color) noexcept;
        void stroke_rect(Rect rect, hcpp::u32 color, hcpp::u32 thickness = 1) noexcept;
        void stroke_rounded_rect(Rect rect, hcpp::i32 radius, hcpp::u32 color) noexcept;
        void fill_circle(hcpp::i32 cx, hcpp::i32 cy, hcpp::i32 radius,
                         hcpp::u32 color) noexcept;
        void stroke_circle(hcpp::i32 cx, hcpp::i32 cy, hcpp::i32 radius,
                           hcpp::u32 color) noexcept;
        void draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text, hcpp::u32 color,
                       const hcpp::fonts::FontFace& font) noexcept;
        void draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text,
                       hcpp::u32 color) noexcept;

    private:
        hcpp::graphics::Framebuffer& target_;
        Rect clip_;
    };

    Size text_extent(hcpp::StringView text, const hcpp::fonts::FontFace& font) noexcept;

}  // namespace hype::ui
