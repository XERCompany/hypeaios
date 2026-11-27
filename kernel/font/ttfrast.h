#pragma once

#include "hcpp/types.hpp"

namespace hype::font::rast {

    struct RastGlyph {
        hcpp::i32 advance_q8 = 0;
        hcpp::i32 bearing_x = 0;
        hcpp::i32 bearing_y = 0;
        hcpp::i32 width = 0;
        hcpp::i32 height = 0;
        const hcpp::u8* alpha = nullptr;
    };

    const RastGlyph* glyph(hcpp::u32 codepoint, hcpp::u32 size_px) noexcept;
    hcpp::i32 advance_q8(hcpp::u32 codepoint, hcpp::u32 size_px) noexcept;
    void reset() noexcept;

}  // namespace hype::font::rast
