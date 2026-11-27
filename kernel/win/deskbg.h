#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

namespace hype::win {

    enum class DeskBgMode : hcpp::u8 { Gradient = 0, Image = 1 };

    constexpr hcpp::u32 desk_bg_wall_width = 1280;
    constexpr hcpp::u32 desk_bg_wall_height = 720;

    bool desk_bg_set_image(const hype::codec::FrameBuf& frame) noexcept;
    void desk_bg_reset() noexcept;
    DeskBgMode desk_bg_mode() noexcept;
    bool desk_bg_ready() noexcept;
    bool desk_bg_paint(hcpp::graphics::Framebuffer& fb) noexcept;
    hcpp::u32 desk_bg_sample(hcpp::u32 x, hcpp::u32 y) noexcept;

}
