#pragma once

#include "hcpp/types.hpp"

namespace hype::font::ttf {

    constexpr hcpp::u32 ttf_max_points = 640;
    constexpr hcpp::u32 ttf_max_contours = 96;

    struct TtfOutline {
        hcpp::u16 point_count = 0;
        hcpp::u16 contour_count = 0;
        hcpp::i16 x_min = 0;
        hcpp::i16 y_min = 0;
        hcpp::i16 x_max = 0;
        hcpp::i16 y_max = 0;
        hcpp::u16 contour_end[ttf_max_contours];
        hcpp::i16 x[ttf_max_points];
        hcpp::i16 y[ttf_max_points];
        hcpp::u8 on_curve[ttf_max_points];
    };

    bool parse(const hcpp::u8* data, hcpp::size_t size) noexcept;
    bool ready() noexcept;
    hcpp::u16 units_per_em() noexcept;
    hcpp::i32 ascent() noexcept;
    hcpp::i32 descent() noexcept;
    hcpp::i32 line_gap() noexcept;
    hcpp::u32 glyph_count() noexcept;
    hcpp::u16 glyph_index(hcpp::u32 codepoint) noexcept;
    hcpp::i32 advance(hcpp::u16 glyph) noexcept;
    hcpp::i32 left_side_bearing(hcpp::u16 glyph) noexcept;
    hcpp::i32 kerning(hcpp::u16 left, hcpp::u16 right) noexcept;
    bool glyph_box(hcpp::u16 glyph, hcpp::i16& x_min, hcpp::i16& y_min,
                   hcpp::i16& x_max, hcpp::i16& y_max) noexcept;
    const TtfOutline* outline(hcpp::u16 glyph) noexcept;

}  // namespace hype::font::ttf
