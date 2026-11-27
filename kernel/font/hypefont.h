#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::font {

    constexpr hcpp::u32 hfnt_magic = 0x544E4648;
    constexpr hcpp::u32 hfnt_max_glyphs = 4096;
    constexpr hcpp::u32 hfnt_max_atlas = 2048;

    struct FontGlyphSdf {
        hcpp::u32 codepoint = 0;
        hcpp::u16 x = 0;
        hcpp::u16 y = 0;
        hcpp::u16 width = 0;
        hcpp::u16 height = 0;
        hcpp::i16 advance = 0;
        hcpp::i8 bearing_x = 0;
        hcpp::i8 bearing_y = 0;

        bool valid() const noexcept;
    };

    extern const hcpp::u8 montserrat_sdf_hfnt[];
    extern const hcpp::u32 montserrat_sdf_hfnt_size;

    bool hypefont_load(const hcpp::u8* data, hcpp::size_t size) noexcept;
    bool hypefont_ready() noexcept;
    FontGlyphSdf font_glyph(hcpp::u32 codepoint) noexcept;
    const hcpp::u8* font_atlas() noexcept;
    hcpp::u32 font_atlas_width() noexcept;
    hcpp::u32 font_atlas_height() noexcept;
    hcpp::u32 font_glyph_count() noexcept;
    hcpp::u32 font_spread() noexcept;
    hcpp::u32 font_em_px() noexcept;
    hcpp::u16 font_line_height() noexcept;
    hcpp::i32 font_kerning(hcpp::u32 left, hcpp::u32 right) noexcept;
    hcpp::i32 font_measure(hcpp::StringView text) noexcept;
    bool font_selftest() noexcept;

}  // namespace hype::font
