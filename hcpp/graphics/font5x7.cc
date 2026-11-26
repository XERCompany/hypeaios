#include "hcpp/graphics/font5x7.hpp"

namespace hcpp::graphics {

    Glyph5x7 glyph_for(u32 codepoint) noexcept { return hcpp::fonts::arial_glyph(codepoint); }

}  // namespace hcpp::graphics
