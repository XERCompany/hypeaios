#pragma once

#include "hcpp/graphics/font5x7.hpp"
#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"

namespace hcpp::graphics {

inline void draw_text(
    Framebuffer& target,
    u32 x,
    u32 y,
    StringView text,
    u32 color,
    u32 scale = 1) noexcept {
    if (scale == 0) {
        return;
    }

    u32 cursor_x = x;
    for (size_t index = 0; index < text.size(); ++index) {
        const Glyph5x7 glyph = glyph_for(text[index]);
        for (u32 row = 0; row < 7; ++row) {
            for (u32 column = 0; column < 5; ++column) {
                if ((glyph.rows[row] >> (4 - column)) & 1U) {
                    target.fill_rect(
                        cursor_x + column * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color);
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

} // namespace hcpp::graphics
