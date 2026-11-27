#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/scaler.h"

namespace hype::display {

    struct WriteFrameOpts {
        ScaleFilter filter = ScaleFilter::Auto;
        bool convert = true;
        bool edge_preserve = false;
        hcpp::u32 dst_x = 0;
        hcpp::u32 dst_y = 0;
        hcpp::u32 dst_w = 0;
        hcpp::u32 dst_h = 0;
    };

    struct FrameStats {
        hcpp::u64 frames = 0;
        hcpp::u64 last_us = 0;
    };

    bool write_frame(hcpp::graphics::Framebuffer& dst,
                     const hcpp::graphics::Framebuffer& src,
                     const WriteFrameOpts& opts) noexcept;
    bool write_frame(hcpp::graphics::Framebuffer& dst,
                     const hcpp::graphics::Framebuffer& src) noexcept;
    const FrameStats& frame_stats() noexcept;
    void frame_stats_reset() noexcept;

}  // namespace hype::display
