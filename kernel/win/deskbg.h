#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"
#include "kernel/ui/geometry.h"

namespace hype::win {

    enum class DeskBgMode : hcpp::u8 { Gradient = 0, Image = 1, Procedural = 2 };

    // Fallback surface size when no scanout target is bound yet; the wall
    // surface is generated at the live framebuffer resolution (capped).
    constexpr hcpp::u32 desk_bg_wall_width = 1280;
    constexpr hcpp::u32 desk_bg_wall_height = 720;
    constexpr hcpp::u32 desk_bg_wall_max_w = 3840;
    constexpr hcpp::u32 desk_bg_wall_max_h = 2160;

    // Image wallpapers: decoded frames are packed then cover-scaled into
    // the wall surface.
    bool desk_bg_set_image(const hype::codec::FrameBuf& frame) noexcept;
    // Procedural pack: "aurora" / "mesh" / "dusk" / "mono" / "nebula".
    bool desk_bg_apply_design(const char* name) noexcept;
    hcpp::i32 desk_bg_design_index(const char* name) noexcept;
    hcpp::u32 desk_bg_design_count() noexcept;
    const char* desk_bg_design_name(hcpp::u32 index) noexcept;
    const char* desk_bg_mode_name() noexcept;
    void desk_bg_surface_size(hcpp::u32& w, hcpp::u32& h) noexcept;
    void desk_bg_reset() noexcept;
    DeskBgMode desk_bg_mode() noexcept;
    bool desk_bg_ready() noexcept;
    bool desk_bg_paint(hcpp::graphics::Framebuffer& fb) noexcept;
    bool desk_bg_paint_clip(hcpp::graphics::Framebuffer& fb,
                            hype::ui::Rect clip) noexcept;
    hcpp::u32 desk_bg_sample(hcpp::u32 x, hcpp::u32 y) noexcept;

}
