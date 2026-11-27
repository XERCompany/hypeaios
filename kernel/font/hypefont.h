#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::font {

    struct FontGlyphMetric {
        hcpp::u32 codepoint = 0;
        hcpp::u16 glyph_index = 0;
        hcpp::i32 advance = 0;
        hcpp::i32 bearing_x = 0;
        hcpp::i32 bearing_y = 0;
        hcpp::i32 width = 0;
        hcpp::i32 height = 0;

        bool valid() const noexcept;
    };

    extern const hcpp::u8 montserrat_ttf_zlib[];
    extern const hcpp::u32 montserrat_ttf_zlib_size;
    extern const hcpp::u32 montserrat_ttf_size;

    bool font_init() noexcept;
    bool hypefont_load(const hcpp::u8* data, hcpp::size_t size) noexcept;
    bool hypefont_ready() noexcept;
    FontGlyphMetric font_glyph(hcpp::u32 codepoint) noexcept;
    hcpp::u32 font_glyph_count() noexcept;
    hcpp::u32 font_em_px() noexcept;
    hcpp::i32 font_ascent() noexcept;
    hcpp::i32 font_descent() noexcept;
    hcpp::i32 font_line_height() noexcept;
    hcpp::i32 font_kerning(hcpp::u32 left, hcpp::u32 right) noexcept;
    hcpp::i32 font_measure(hcpp::StringView text) noexcept;
    bool font_selftest() noexcept;

}  // namespace hype::font
