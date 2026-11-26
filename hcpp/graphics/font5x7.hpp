#pragma once

#include "hcpp/types.hpp"

namespace hcpp::graphics {

struct Glyph5x7 {
    u8 rows[7];
};

Glyph5x7 glyph_for(char character) noexcept;

} // namespace hcpp::graphics
