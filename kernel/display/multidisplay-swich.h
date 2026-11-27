#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/idscreen.h"
#include "kernel/display/multidisplay-mode.h"
#include "kernel/display/multidisplay.h"

namespace hype::display {

    struct MultiVirtualRect {
        hcpp::u32 width = 0;
        hcpp::u32 height = 0;
    };

    bool multi_set_mode(MultiMode mode) noexcept;
    MultiMode multi_mode() noexcept;
    bool multi_switch_active(OutputId id) noexcept;
    OutputId multi_active_output() noexcept;
    MultiVirtualRect multi_virtual_rect() noexcept;
    hcpp::u32 multi_present(const hcpp::graphics::Framebuffer& src) noexcept;

}  // namespace hype::display
