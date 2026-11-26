#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/hello/hello.h"

namespace hype::ui::flowlogin {

    enum class State : hcpp::u8 { Hidden, Shown, Authenticated, Unavailable };

    bool ensure_demo_user() noexcept;
    hcpp::u32 show(hcpp::graphics::Framebuffer& framebuffer) noexcept;
    void repaint(hcpp::graphics::Framebuffer& framebuffer) noexcept;
    bool handle_char(char ascii) noexcept;
    hello::HelloResult submit() noexcept;
    State state() noexcept;
    bool authenticated() noexcept;
    hello::HelloResult last_result() noexcept;
    hcpp::u32 drive(hcpp::graphics::Framebuffer& framebuffer) noexcept;

}  // namespace hype::ui::flowlogin
