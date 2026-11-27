#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/input/curstat.h"

namespace hype::input {

    bool cursor_init(hcpp::u32 screen_w, hcpp::u32 screen_h) noexcept;
    bool cursor_ready() noexcept;
    bool cursor_poll() noexcept;
    hcpp::u32 cursor_draw(hcpp::graphics::Framebuffer &fb) noexcept;
    CurStat cursor_stat() noexcept;
    bool input_selftest(hcpp::graphics::Framebuffer &fb) noexcept;

}
