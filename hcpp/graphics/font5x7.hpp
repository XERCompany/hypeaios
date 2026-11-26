#pragma once

#include "hcpp/fonts/font.hpp"

namespace hcpp::graphics {

using Glyph5x7 = hcpp::fonts::FontGlyph;

Glyph5x7 glyph_for(char character) noexcept;

} // namespace hcpp::graphics
