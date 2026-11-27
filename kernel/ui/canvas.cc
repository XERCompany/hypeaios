#include "kernel/ui/canvas.h"

#include "hcpp/utf8.hpp"

namespace hype::ui {

    namespace {

        hcpp::i32 isqrt_i32(hcpp::i32 value) noexcept {
            if (value <= 0) {
                return 0;
            }
            const hcpp::u32 v = static_cast<hcpp::u32>(value);
            hcpp::u32 x = v;
            hcpp::u32 y = (x + 1) / 2;
            while (y < x) {
                x = y;
                y = (x + v / x) / 2;
            }
            return static_cast<hcpp::i32>(x);
        }

        hcpp::i32 clamp_radius(Rect rect, hcpp::i32 radius) noexcept {
            const hcpp::i32 half = (rect.w < rect.h ? rect.w : rect.h) / 2;
            if (radius < 0) {
                return 0;
            }
            return radius > half ? half : radius;
        }

    }  // namespace

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

    void Canvas::draw_line(hcpp::i32 x0, hcpp::i32 y0, hcpp::i32 x1, hcpp::i32 y1,
                           hcpp::u32 color) noexcept {
        const hcpp::i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
        const hcpp::i32 step_x = x0 < x1 ? 1 : -1;
        const hcpp::i32 dy = y1 > y0 ? y0 - y1 : y1 - y0;
        const hcpp::i32 step_y = y0 < y1 ? 1 : -1;
        hcpp::i32 error = dx + dy;
        for (;;) {
            draw_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                return;
            }
            const hcpp::i32 twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x0 += step_x;
            }
            if (twice <= dx) {
                error += dx;
                y0 += step_y;
            }
        }
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

    void Canvas::fill_rect_alpha(Rect rect, hcpp::u32 color, hcpp::u8 alpha) noexcept {
        if (alpha == 0) {
            return;
        }
        if (alpha == 255) {
            fill_rect(rect, color);
            return;
        }
        const Rect clipped = clip_.intersection(rect);
        if (clipped.empty()) {
            return;
        }
        hcpp::u8 src_red = 0;
        hcpp::u8 src_green = 0;
        hcpp::u8 src_blue = 0;
        target_.decode_rgb(color, src_red, src_green, src_blue);
        const hcpp::u32 blend = alpha;
        const hcpp::u32 inverse = 255 - blend;
        for (hcpp::i32 row = clipped.y; row < clipped.bottom(); ++row) {
            for (hcpp::i32 column = clipped.x; column < clipped.right(); ++column) {
                const hcpp::u32 dst = target_.get_pixel(static_cast<hcpp::u32>(column),
                                                        static_cast<hcpp::u32>(row));
                hcpp::u8 dst_red = 0;
                hcpp::u8 dst_green = 0;
                hcpp::u8 dst_blue = 0;
                target_.decode_rgb(dst, dst_red, dst_green, dst_blue);
                const hcpp::u8 red = static_cast<hcpp::u8>(
                    (src_red * blend + dst_red * inverse) / 255);
                const hcpp::u8 green = static_cast<hcpp::u8>(
                    (src_green * blend + dst_green * inverse) / 255);
                const hcpp::u8 blue = static_cast<hcpp::u8>(
                    (src_blue * blend + dst_blue * inverse) / 255);
                target_.put_pixel(static_cast<hcpp::u32>(column),
                                  static_cast<hcpp::u32>(row),
                                  target_.encode_rgb(red, green, blue));
            }
        }
    }

    void Canvas::fill_rounded_rect(Rect rect, hcpp::i32 radius, hcpp::u32 color) noexcept {
        if (rect.empty()) {
            return;
        }
        const hcpp::i32 r = clamp_radius(rect, radius);
        if (r == 0) {
            fill_rect(rect, color);
            return;
        }
        const hcpp::i32 rr = r * r;
        const hcpp::i32 top_center = rect.y + r;
        const hcpp::i32 bottom_center = rect.bottom() - r - 1;
        for (hcpp::i32 row = rect.y; row < rect.bottom(); ++row) {
            hcpp::i32 distance = 0;
            if (row < top_center) {
                distance = top_center - row;
            } else if (row > bottom_center) {
                distance = row - bottom_center;
            }
            hcpp::i32 inset = 0;
            if (distance > 0) {
                inset = r - isqrt_i32(rr - distance * distance);
            }
            fill_rect({rect.x + inset, row, rect.w - 2 * inset, 1}, color);
        }
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

    void Canvas::stroke_rounded_rect(Rect rect, hcpp::i32 radius, hcpp::u32 color) noexcept {
        if (rect.empty()) {
            return;
        }
        const hcpp::i32 r = clamp_radius(rect, radius);
        if (r == 0) {
            stroke_rect(rect, color);
            return;
        }
        fill_rect({rect.x + r, rect.y, rect.w - 2 * r, 1}, color);
        fill_rect({rect.x + r, rect.bottom() - 1, rect.w - 2 * r, 1}, color);
        fill_rect({rect.x, rect.y + r, 1, rect.h - 2 * r}, color);
        fill_rect({rect.right() - 1, rect.y + r, 1, rect.h - 2 * r}, color);

        const Rect saved_clip = clip_;
        set_clip(Rect{rect.x, rect.y, r + 1, r + 1});
        stroke_circle(rect.x + r, rect.y + r, r, color);
        set_clip(Rect{rect.right() - r - 1, rect.y, r + 1, r + 1});
        stroke_circle(rect.right() - r - 1, rect.y + r, r, color);
        set_clip(Rect{rect.x, rect.bottom() - r - 1, r + 1, r + 1});
        stroke_circle(rect.x + r, rect.bottom() - r - 1, r, color);
        set_clip(Rect{rect.right() - r - 1, rect.bottom() - r - 1, r + 1, r + 1});
        stroke_circle(rect.right() - r - 1, rect.bottom() - r - 1, r, color);
        set_clip(saved_clip);
    }

    void Canvas::fill_circle(hcpp::i32 cx, hcpp::i32 cy, hcpp::i32 radius,
                             hcpp::u32 color) noexcept {
        if (radius < 0) {
            return;
        }
        if (radius == 0) {
            draw_pixel(cx, cy, color);
            return;
        }
        const hcpp::i32 rr = radius * radius;
        for (hcpp::i32 dy = -radius; dy <= radius; ++dy) {
            const hcpp::i32 half = isqrt_i32(rr - dy * dy);
            fill_rect({cx - half, cy + dy, 2 * half + 1, 1}, color);
        }
    }

    void Canvas::stroke_circle(hcpp::i32 cx, hcpp::i32 cy, hcpp::i32 radius,
                               hcpp::u32 color) noexcept {
        if (radius < 0) {
            return;
        }
        hcpp::i32 x = radius;
        hcpp::i32 y = 0;
        hcpp::i32 error = 1 - radius;
        while (x >= y) {
            draw_pixel(cx + x, cy + y, color);
            draw_pixel(cx - x, cy + y, color);
            draw_pixel(cx + x, cy - y, color);
            draw_pixel(cx - x, cy - y, color);
            draw_pixel(cx + y, cy + x, color);
            draw_pixel(cx - y, cy + x, color);
            draw_pixel(cx + y, cy - x, color);
            draw_pixel(cx - y, cy - x, color);
            ++y;
            if (error < 0) {
                error += 2 * y + 1;
            } else {
                --x;
                error += 2 * (y - x) + 1;
            }
        }
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

    Size text_extent(hcpp::StringView text, const hcpp::fonts::FontFace& font) noexcept {
        Size extent{0, font.height};
        if (font.glyph_for == nullptr) {
            return extent;
        }
        hcpp::size_t cursor = 0;
        while (cursor < text.size()) {
            const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
            const hcpp::fonts::FontGlyph glyph = font.glyph_for(codepoint);
            extent.width += glyph.advance != 0 ? glyph.advance : font.advance;
        }
        return extent;
    }

}  // namespace hype::ui
