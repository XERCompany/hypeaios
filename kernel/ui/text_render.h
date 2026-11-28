#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    inline hcpp::i32 draw_text_sdf(Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                                   hcpp::StringView text, hcpp::u32 color,
                                   hcpp::u32 size_px) noexcept {
        return hype::font::text_draw_clip(canvas.target(), x, y, text, color,
                                          size_px, canvas.clip());
    }

    inline hcpp::i32 draw_text_sdf_styled(Canvas& canvas, hcpp::i32 x,
                                          hcpp::i32 y, hcpp::StringView text,
                                          hcpp::u32 color, hcpp::u32 size_px,
                                          hcpp::u8 weight,
                                          bool italic) noexcept {
        // Clip to the canvas damage rect: without it styled glyphs can
        // overdraw neighbouring widgets and pixels outside the repaint.
        return hype::font::text_draw_styled_clip(canvas.target(), x, y, text,
                                                 color, size_px, weight, italic,
                                                 canvas.clip());
    }

    inline hcpp::i32 draw_text_sdf_wrapped(Canvas& canvas, Rect rect,
                                           hcpp::StringView text,
                                           hype::font::TextAlign align,
                                           hcpp::u32 size_px) noexcept {
        return hype::font::text_draw_wrapped(canvas.target(), rect, text, align,
                                             size_px);
    }

    inline void draw_text_sdf_shadow(Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                                     hcpp::StringView text, hcpp::u32 size_px,
                                     hcpp::u32 color, hcpp::i32 shadow_offset,
                                     hcpp::i32 blur) noexcept {
        hype::font::text_shadow(canvas.target(), x, y, text, size_px, color,
                                shadow_offset, blur);
    }

    inline hcpp::i32 text_sdf_measure(hcpp::StringView text,
                                      hcpp::u32 size_px) noexcept {
        return hype::font::text_measure(text, size_px);
    }

}  // namespace hype::ui
