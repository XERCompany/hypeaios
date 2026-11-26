#include "hcpp/graphics/font5x7.hpp"

namespace hcpp::graphics {

    Glyph5x7 glyph_for(char character) noexcept { return hcpp::fonts::arial_glyph(character); }

}  // namespace hcpp::graphics
