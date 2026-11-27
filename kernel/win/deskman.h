#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"

namespace hype::win {

    hcpp::u32 desk_open_app(const char* name) noexcept;
    hcpp::u32 desk_paint(hcpp::graphics::Framebuffer& fb) noexcept;
    hcpp::u32 desk_window_count() noexcept;

}
