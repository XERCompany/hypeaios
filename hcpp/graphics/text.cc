#include "hcpp/graphics/text.hpp"
#include "hcpp/utf8.hpp"

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
        size_t cursor = 0;
        while (cursor < text.size()) {
            const u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
            const hcpp::fonts::FontGlyph glyph = font.glyph_for(codepoint);
            const i64 origin_x =
                static_cast<i64>(cursor_x) + static_cast<i64>(glyph.bearing_x) * scale;
            const i64 origin_y = static_cast<i64>(y) +
                                 (static_cast<i64>(font.baseline) - static_cast<i64>(glyph.bearing_y)) *
                                     scale;
            for (u32 row = 0; row < glyph.height; ++row) {
                for (u32 column = 0; column < glyph.width; ++column) {
                    if (!glyph.pixel(column, row)) {
                        continue;
                    }
                    const i64 pixel_x = origin_x + column * scale;
                    const i64 pixel_y = origin_y + row * scale;
                    if (pixel_x < 0 || pixel_y < 0) {
                        continue;
                    }
                    target.fill_rect(static_cast<u32>(pixel_x), static_cast<u32>(pixel_y), scale, scale,
                                     color);
                }
            }
            const u8 advance = glyph.advance != 0 ? glyph.advance : font.advance;
            cursor_x += advance * scale;
        }
    }

}  // namespace hcpp::graphics
