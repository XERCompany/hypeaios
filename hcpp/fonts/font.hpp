#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp::fonts {

    struct FontGlyph {
        const u8* bitmap;
        u8 row_stride;
        u8 width;
        u8 height;
        u8 advance;
        i8 bearing_x;
        i8 bearing_y;

        bool valid() const noexcept;
        const u8* row_data(u32 row) const noexcept;
        bool pixel(u32 column, u32 row) const noexcept;
    };

    using GlyphLookup = FontGlyph (*)(u32 codepoint) noexcept;

    struct FontFace {
        const char* name;
        u8 height;
        u8 baseline;
        u8 advance;
        u16 glyph_count;
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

    FontGlyph arial_glyph(u32 codepoint) noexcept;
    const FontFace& arial_font() noexcept;

}  // namespace hcpp::fonts
