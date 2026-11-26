#include "kernel/ui/canvas.h"

#include "hcpp/utf8.hpp"

namespace hype::ui {

    Canvas::Canvas(hcpp::graphics::Framebuffer& target) noexcept : target_(target) { reset_clip(); }

    hcpp::graphics::Framebuffer& Canvas::target() const noexcept { return target_; }

    hcpp::u32 Canvas::color(hcpp::u32 rgb) const noexcept {
        return target_.encode_rgb(static_cast<hcpp::u8>((rgb >> 16) & 0xFF),
                                  static_cast<hcpp::u8>((rgb >> 8) & 0xFF),
                                  static_cast<hcpp::u8>(rgb & 0xFF));
    }

    void Canvas::set_clip(Rect clip) noexcept {
        const Rect bounds{0, 0, static_cast<hcpp::i32>(target_.width()),
                          static_cast<hcpp::i32>(target_.height())};
        clip_ = bounds.intersection(clip);
    }

    void Canvas::reset_clip() noexcept {
        clip_ = Rect{0, 0, static_cast<hcpp::i32>(target_.width()),
                     static_cast<hcpp::i32>(target_.height())};
    }

    Rect Canvas::clip() const noexcept { return clip_; }

    void Canvas::draw_pixel(hcpp::i32 x, hcpp::i32 y, hcpp::u32 color) noexcept {
        if (!clip_.contains(Point{x, y})) {
            return;
        }
        target_.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y), color);
    }

    void Canvas::fill_rect(Rect rect, hcpp::u32 color) noexcept {
        const Rect clipped = clip_.intersection(rect);
        if (clipped.empty()) {
            return;
        }
        target_.fill_rect(static_cast<hcpp::u32>(clipped.x), static_cast<hcpp::u32>(clipped.y),
                          static_cast<hcpp::u32>(clipped.w), static_cast<hcpp::u32>(clipped.h),
                          color);
    }

    void Canvas::stroke_rect(Rect rect, hcpp::u32 color, hcpp::u32 thickness) noexcept {
        if (rect.empty() || thickness == 0) {
            return;
        }
        const hcpp::i32 edge = static_cast<hcpp::i32>(thickness);
        fill_rect({rect.x, rect.y, rect.w, edge}, color);
        fill_rect({rect.x, rect.bottom() - edge, rect.w, edge}, color);
        const hcpp::i32 side_height = rect.h - 2 * edge;
        if (side_height <= 0) {
            return;
        }
        fill_rect({rect.x, rect.y + edge, edge, side_height}, color);
        fill_rect({rect.right() - edge, rect.y + edge, edge, side_height}, color);
    }

    void Canvas::draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text,
                           hcpp::u32 color, const hcpp::fonts::FontFace& font) noexcept {
        if (font.glyph_for == nullptr || clip_.empty()) {
            return;
        }

        hcpp::i32 cursor_x = x;
        hcpp::size_t cursor = 0;
        while (cursor < text.size()) {
            const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
            const hcpp::fonts::FontGlyph glyph = font.glyph_for(codepoint);
            const hcpp::u8 advance = glyph.advance != 0 ? glyph.advance : font.advance;
            if (glyph.valid()) {
                const hcpp::i32 origin_x = cursor_x + glyph.bearing_x;
                const hcpp::i32 origin_y = baseline_y - glyph.bearing_y;
                const Rect clipped =
                    clip_.intersection(Rect{origin_x, origin_y, glyph.width, glyph.height});
                for (hcpp::i32 row = clipped.y; row < clipped.y + clipped.h; ++row) {
                    for (hcpp::i32 column = clipped.x; column < clipped.x + clipped.w; ++column) {
                        if (glyph.pixel(static_cast<hcpp::u32>(column - origin_x),
                                        static_cast<hcpp::u32>(row - origin_y))) {
                            target_.put_pixel(static_cast<hcpp::u32>(column),
                                              static_cast<hcpp::u32>(row), color);
                        }
                    }
                }
            }
            cursor_x += advance;
        }
    }

    void Canvas::draw_text(hcpp::i32 x, hcpp::i32 baseline_y, hcpp::StringView text,
                           hcpp::u32 color) noexcept {
        const hcpp::fonts::FontFace* font = hcpp::fonts::default_font();
        if (font == nullptr) {
            return;
        }
        draw_text(x, baseline_y, text, color, *font);
    }

}  // namespace hype::ui
