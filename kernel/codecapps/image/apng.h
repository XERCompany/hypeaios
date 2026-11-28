#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    // APNG (PNG + acTL/fcTL/fdAT, W3C png-3): animated frame iterator —
    // each frame's zlib payload (IDAT or fdAT-minus-seq) goes through the
    // shared PNG scanline pipeline, then composites onto the canvas
    // honoring dispose_op (0 none / 1 background / 2 previous) and
    // blend_op (0 source / 1 over). Mirrors gif.cc's GifAnim.
    struct ApngAnim {
        const hcpp::u8 *src;
        hcpp::size_t size;
        hcpp::size_t pos;          // chunk cursor
        hcpp::u32 canvas_w, canvas_h;
        hcpp::u8 color_type;
        const hcpp::u8 *palette;   // PLTE payload (nullptr if truecolor)
        hcpp::size_t palette_size;
        hcpp::u32 frames_declared; // acTL num_frames
        hcpp::u32 plays;           // acTL num_plays (0 = forever)
        hcpp::u32 frames_done;
        hcpp::u16 delay_ms;        // delay of last rendered frame
        // disposal bookkeeping for the previous frame's rect
        hcpp::u8 prev_dispose;
        hcpp::u32 prev_x, prev_y, prev_w, prev_h;
        hcpp::u8 *restore;         // dispose-2 canvas snapshot (arena)
        hcpp::size_t restore_cap;
    };

    // Returns true when the stream is PNG carrying an acTL chunk.
    bool apng_probe(const hcpp::u8 *src, hcpp::size_t size) noexcept;

    bool apng_anim_open(ApngAnim &anim, const hcpp::u8 *src, hcpp::size_t size) noexcept;

    // Composites the next frame onto `canvas` (prepared on first call,
    // canvas-size FrameBuf, transparent-black initial). `delay_ms`
    // receives the fcTL delay. Returns false at IEND or on malformed data.
    bool apng_anim_next(ApngAnim &anim, FrameBuf &canvas, hcpp::u16 &delay_ms,
                        Arena &arena) noexcept;

    // acTL num_frames, or 0 when not animated.
    hcpp::u32 apng_frame_count(const hcpp::u8 *src, hcpp::size_t size) noexcept;

    // Registry ABI: decodes the first composited animation frame (or the
    // default image when the animation carries no frames).
    CodecStatus apng_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                            void *output, Arena &arena) noexcept;

}  // namespace hype::codec::image
