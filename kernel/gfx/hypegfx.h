#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {
    class Canvas;
}

namespace hype::gfx {

    void gfx_soft_shadow(hype::ui::Canvas& canvas, hype::ui::Rect rect,
                         hcpp::i32 radius, hcpp::i32 blur, hcpp::u8 alpha) noexcept;
    void gfx_inner_shadow(hype::ui::Canvas& canvas, hype::ui::Rect rect,
                          hcpp::i32 radius, hcpp::i32 blur) noexcept;
    void gfx_gradient_v(hype::ui::Canvas& canvas, hype::ui::Rect rect, hcpp::u32 c1,
                        hcpp::u32 c2) noexcept;
    void gfx_gradient_h(hype::ui::Canvas& canvas, hype::ui::Rect rect, hcpp::u32 c1,
                        hcpp::u32 c2) noexcept;
    void gfx_gradient_radial(hype::ui::Canvas& canvas, hype::ui::Rect rect,
                             hcpp::u32 c_center, hcpp::u32 c_edge) noexcept;
    void gfx_elevation(hype::ui::Canvas& canvas, hype::ui::Rect rect,
                       hcpp::u32 level) noexcept;
    void gfx_glow(hype::ui::Canvas& canvas, hype::ui::Rect rect, hcpp::u32 color,
                  hcpp::i32 radius) noexcept;

    bool gfx_selftest() noexcept;

}  // namespace hype::gfx
