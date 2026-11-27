#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"

namespace hype::input {

    constexpr hcpp::i32 cursor_sprite_w = 14;
    constexpr hcpp::i32 cursor_sprite_h = 24;

    hcpp::u32 cursprite_draw(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y) noexcept;
    hcpp::u32 cursprite_opaque_count() noexcept;
    hcpp::u32 cursprite_test_fill(hcpp::u32 *pixels, hcpp::u32 capacity) noexcept;

}
