#include "hcpp/graphics/text.hpp"

namespace hcpp::graphics {

    void draw_text(Framebuffer& target, u32 x, u32 y, StringView text, u32 color, u32 scale) noexcept {
        const hcpp::fonts::FontFace* font = hcpp::fonts::default_font();
        if (font == nullptr) {
            return;
        }
        draw_text_with_font(target, x, y, text, color, scale, *font);
    }

    void draw_text_with_font(Framebuffer& target, u32 x, u32 y, StringView text, u32 color, u32 scale,
                             const hcpp::fonts::FontFace& font) noexcept {
        if (scale == 0 || font.glyph_for == nullptr) {
            return;
        }

        u32 cursor_x = x;
        for (size_t index = 0; index < text.size(); ++index) {
            const hcpp::fonts::FontGlyph glyph = font.glyph_for(text[index]);
            for (u32 row = 0; row < font.height && row < 7; ++row) {
                for (u32 column = 0; column < font.width && column < 8; ++column) {
                    if ((glyph.rows[row] >> (font.width - 1 - column)) & 1U) {
                        target.fill_rect(cursor_x + column * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
            cursor_x += font.advance * scale;
        }
    }

}  // namespace hcpp::graphics
