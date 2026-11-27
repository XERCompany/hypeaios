#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/idscreen.h"

namespace hype::display {

    bool scrnmap_add(OutputId output, hcpp::graphics::Framebuffer& fb) noexcept;
    hcpp::graphics::Framebuffer* scrnmap_fb(OutputId output) noexcept;
    bool scrnmap_remove(OutputId output) noexcept;
    hcpp::u32 scrnmap_count() noexcept;

}  // namespace hype::display
