#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp::fonts {

struct FontGlyph {
    u8 rows[7];
};

using GlyphLookup = FontGlyph (*)(char character) noexcept;

struct FontFace {
    const char* name;
    u8 width;
    u8 height;
    u8 advance;
    GlyphLookup glyph_for;
};

constexpr size_t max_registered_fonts = 8;

void initialize_default_fonts() noexcept;
bool register_font(const FontFace& font) noexcept;
const FontFace* find_font(StringView name) noexcept;
const FontFace* default_font() noexcept;
bool set_default_font(StringView name) noexcept;
StringView default_font_name() noexcept;
size_t registered_font_count() noexcept;

FontGlyph arial_glyph(char character) noexcept;
const FontFace& arial_font() noexcept;

} // namespace hcpp::fonts
