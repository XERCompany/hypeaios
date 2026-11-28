#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::font {

    enum class TextAlign : hcpp::u8 {
        Left,
        Center,
        Right,
    };

    hcpp::u32 text_scale_q8(hcpp::u32 size_px) noexcept;
    hcpp::i32 text_measure(hcpp::StringView text, hcpp::u32 size_px) noexcept;
    hcpp::u32 text_line_height(hcpp::u32 size_px) noexcept;
    hcpp::i32 text_draw(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                        hcpp::StringView text, hcpp::u32 color,
                        hcpp::u32 size_px) noexcept;
    hcpp::i32 text_draw_clip(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                             hcpp::StringView text, hcpp::u32 color, hcpp::u32 size_px,
                             hype::ui::Rect clip) noexcept;
    hcpp::i32 text_draw_styled(hcpp::graphics::Framebuffer& fb, hcpp::i32 x,
                               hcpp::i32 y, hcpp::StringView text, hcpp::u32 color,
                               hcpp::u32 size_px, hcpp::u8 weight,
                               bool italic) noexcept;
    // Same as text_draw_styled but glyphs are clipped to `clip` — the canvas
    // damage rect — so text can never write outside the invalidated region.
    hcpp::i32 text_draw_styled_clip(hcpp::graphics::Framebuffer& fb, hcpp::i32 x,
                                    hcpp::i32 y, hcpp::StringView text,
                                    hcpp::u32 color, hcpp::u32 size_px,
                                    hcpp::u8 weight, bool italic,
                                    hype::ui::Rect clip) noexcept;
    hcpp::i32 text_draw_wrapped(hcpp::graphics::Framebuffer& fb, hype::ui::Rect rect,
                                hcpp::StringView text, TextAlign align,
                                hcpp::u32 size_px) noexcept;
    void text_shadow(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                     hcpp::StringView text, hcpp::u32 size_px, hcpp::u32 color,
                     hcpp::i32 shadow_offset, hcpp::i32 blur) noexcept;
    bool text_selftest() noexcept;

}  // namespace hype::font
