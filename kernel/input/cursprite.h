#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"

namespace hype::input {

    constexpr hcpp::i32 cursor_sprite_w = 14;
    constexpr hcpp::i32 cursor_sprite_h = 24;
    constexpr hcpp::i32 cursor_png_sprite_w = 32;
    constexpr hcpp::i32 cursor_png_sprite_h = 32;

    extern const hcpp::u8 cursor_png_data[];
    extern const hcpp::u32 cursor_png_data_size;

    bool cursprite_init() noexcept;
    bool cursprite_png_active() noexcept;
    hcpp::u32 cursprite_draw(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y) noexcept;
    hcpp::u32 cursprite_erase(hcpp::graphics::Framebuffer &fb) noexcept;
    hcpp::u32 cursprite_opaque_count() noexcept;
    hcpp::u32 cursprite_test_fill(hcpp::u32 *pixels, hcpp::u32 capacity) noexcept;

}
