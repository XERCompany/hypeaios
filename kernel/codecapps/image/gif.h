#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    // Animated GIF iterator (issue #180): parses Graphic Control Extension
    // delay/disposal/transparency and the NETSCAPE2.0 loop count, and
    // composites each frame onto a logical-screen-sized FrameBuf canvas
    // (disposal 1 leave / 2 restore-background / 3 restore-previous).
    struct GifAnim {
        const hcpp::u8 *src;
        hcpp::size_t size;
        hcpp::size_t pos;
        const hcpp::u8 *gct;
        hcpp::u32 gct_size;
        hcpp::u32 screen_w, screen_h;
        hcpp::u8 bg_index;
        hcpp::u16 loop_count;   // 0xFFFF = no NETSCAPE ext; 0 = loop forever
        hcpp::u16 delay_cs;     // delay of the last rendered frame (1/100 s)
        hcpp::u32 frames;       // frames rendered so far
        hcpp::u8 disposal;      // pending disposal from the last GCE
        hcpp::u8 transparent;
        bool has_transparent;
        hcpp::u8 prev_disposal;
        hcpp::u32 prev_x, prev_y, prev_w, prev_h;
        hcpp::u8 *restore;      // disposal-3 canvas snapshot (arena)
        hcpp::size_t restore_cap;
    };

    CodecStatus gif_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;

    bool gif_anim_open(GifAnim &anim, const hcpp::u8 *src, hcpp::size_t size) noexcept;

    // Renders the next frame into `canvas` (allocated/prepared on first call).
    // Returns false at the trailer or on malformed input. `delay_cs` receives
    // the frame's GCE delay in centiseconds (0 when unspecified).
    bool gif_anim_next(GifAnim &anim, FrameBuf &canvas, hcpp::u16 &delay_cs,
                       Arena &arena) noexcept;

    // Counts image descriptors without decoding pixels.
    hcpp::u32 gif_frame_count(const hcpp::u8 *src, hcpp::size_t size) noexcept;

}  // namespace hype::codec::image
