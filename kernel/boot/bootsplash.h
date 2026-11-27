#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::boot {

    void boot_draw_text(hcpp::graphics::Framebuffer& fb, hcpp::u32 x, hcpp::u32 y,
                        hcpp::StringView text, hcpp::u32 color, hcpp::u32 scale) noexcept;

    void boot_splash_begin(hcpp::graphics::Framebuffer& fb) noexcept;
    void boot_splash_hold() noexcept;
    void boot_splash_stage(hcpp::StringView name, hcpp::u32 pct) noexcept;
    void boot_splash_spinner() noexcept;
    void boot_splash_end() noexcept;

}
