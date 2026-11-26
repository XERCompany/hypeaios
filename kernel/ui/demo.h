#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"

namespace hype::ui {

    hcpp::u32 demo(hcpp::graphics::Framebuffer& framebuffer) noexcept;
    bool demo_codec_image_painted() noexcept;

}  // namespace hype::ui
