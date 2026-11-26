#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp::graphics {

    void draw_text(Framebuffer& target, u32 x, u32 y, StringView text, u32 color, u32 scale = 1) noexcept;

    void draw_text_with_font(Framebuffer& target, u32 x, u32 y, StringView text, u32 color, u32 scale,
                             const hcpp::fonts::FontFace& font) noexcept;

}  // namespace hcpp::graphics
