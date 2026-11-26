#pragma once

#include "hcpp/fonts/font.hpp"

namespace hcpp::graphics {

    using Glyph5x7 = hcpp::fonts::FontGlyph;

    Glyph5x7 glyph_for(u32 codepoint) noexcept;

}  // namespace hcpp::graphics
