#include "kernel/font/hypetext.h"

#include "hcpp/memory.hpp"
#include "hcpp/utf8.hpp"
#include "kernel/font/hypefont.h"
#include "kernel/font/ttfrast.h"
#include "kernel/serial.hpp"
#include "kernel/ui/uiscale.h"

namespace hype::font {

    namespace {

        using hype::ui::Point;
        using hype::ui::Rect;

        constexpr hcpp::i32 k_mask_w = 384;
        constexpr hcpp::i32 k_mask_h = 128;
        constexpr hcpp::i32 k_italic_shear_q8 = 51;
        hcpp::u8 text_mask[k_mask_w * k_mask_h];

        struct Plot {
            hcpp::graphics::Framebuffer* fb = nullptr;
            hcpp::u8* mask = nullptr;
            hcpp::i32 mask_w = 0;
            hcpp::i32 mask_h = 0;
            hcpp::i32 off_x = 0;
            hcpp::i32 off_y = 0;
            Rect clip{};
            hcpp::u8 red = 0;
            hcpp::u8 green = 0;
            hcpp::u8 blue = 0;
        };

        hcpp::i32 clamp_i32(hcpp::i32 value, hcpp::i32 low, hcpp::i32 high) noexcept {
            if (value < low) {
                return low;
            }
            return value > high ? high : value;
        }

        void plot_put(const Plot& plot, hcpp::i32 x, hcpp::i32 y,
                      hcpp::u32 alpha) noexcept {
            if (alpha == 0 || !plot.clip.contains(Point{x, y})) {
                return;
            }
            if (plot.mask != nullptr) {
                const hcpp::i32 mx = x - plot.off_x;
                const hcpp::i32 my = y - plot.off_y;
                if (static_cast<hcpp::u32>(mx) < static_cast<hcpp::u32>(plot.mask_w) &&
                    static_cast<hcpp::u32>(my) < static_cast<hcpp::u32>(plot.mask_h)) {
                    plot.mask[my * plot.mask_w + mx] = static_cast<hcpp::u8>(
                        alpha > 255 ? 255 : alpha);
                }
                return;
            }
            const hcpp::u32 dst = plot.fb->get_pixel(static_cast<hcpp::u32>(x),
                                                     static_cast<hcpp::u32>(y));
            hcpp::u8 dst_red = 0;
            hcpp::u8 dst_green = 0;
            hcpp::u8 dst_blue = 0;
            plot.fb->decode_rgb(dst, dst_red, dst_green, dst_blue);
            const hcpp::u32 inverse = 255 - alpha;
            // +127: round-to-nearest so antialiased edges do not drift dark
            // relative to the cursor/over blend paths that already round.
            plot.fb->put_pixel(
                static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                plot.fb->encode_rgb(
                    static_cast<hcpp::u8>(
                        (plot.red * alpha + dst_red * inverse + 127) / 255),
                    static_cast<hcpp::u8>(
                        (plot.green * alpha + dst_green * inverse + 127) / 255),
                    static_cast<hcpp::u8>(
                        (plot.blue * alpha + dst_blue * inverse + 127) / 255)));
        }

        hcpp::i32 units_to_q8(hcpp::i32 units, hcpp::u32 size_px) noexcept {
            const hcpp::u32 em = font_em_px();
            if (em == 0) {
                return 0;
            }
            return static_cast<hcpp::i32>(
                static_cast<hcpp::i64>(units) *
                    (static_cast<hcpp::i64>(size_px) << 8) / em);
        }

        hcpp::i32 q8_to_px(hcpp::i32 value_q8) noexcept {
            return (value_q8 + 128) >> 8;
        }

        hcpp::i32 fallback_advance_q8(hcpp::u32 size_px) noexcept {
            const hcpp::i32 fallback = rast::advance_q8('?', size_px);
            if (fallback != 0) {
                return fallback;
            }
            return units_to_q8(static_cast<hcpp::i32>(font_em_px()) / 2,
                               size_px);
        }

        hcpp::i32 span_advance_q8(hcpp::StringView text,
                                  hcpp::u32 size_px) noexcept {
            hcpp::i32 width = 0;
            hcpp::u32 previous = 0;
            hcpp::size_t cursor = 0;
            while (cursor < text.size()) {
                const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
                if (previous != 0) {
                    width += units_to_q8(font_kerning(previous, codepoint),
                                         size_px);
                }
                const hcpp::i32 advance = rast::advance_q8(codepoint, size_px);
                width += advance != 0 ? advance
                                      : fallback_advance_q8(size_px);
                previous = codepoint;
            }
            return width;
        }

        void blit_glyph(const Plot& plot, const rast::RastGlyph& glyph,
                        hcpp::i32 pen_x, hcpp::i32 baseline_y,
                        hcpp::i32 bold_shift, bool italic) noexcept {
            const hcpp::i32 top = baseline_y - glyph.bearing_y;
            const hcpp::i32 base_x = pen_x + glyph.bearing_x;
            for (hcpp::i32 row = 0; row < glyph.height; ++row) {
                const hcpp::i32 py = top + row;
                const hcpp::i32 shear =
                    italic ? ((baseline_y - py) * k_italic_shear_q8) >> 8 : 0;
                const hcpp::u8* src = glyph.alpha + row * glyph.width;
                for (hcpp::i32 col = 0; col < glyph.width; ++col) {
                    const hcpp::u32 alpha = src[col];
                    if (alpha == 0) {
                        continue;
                    }
                    const hcpp::i32 px = base_x + col + shear;
                    plot_put(plot, px, py, alpha);
                    for (hcpp::i32 extra = 1; extra <= bold_shift; ++extra) {
                        plot_put(plot, px + extra, py, alpha);
                    }
                }
            }
        }

        hcpp::i32 draw_run(const Plot& plot, hcpp::i32 x, hcpp::i32 baseline_y,
                           hcpp::StringView text, hcpp::u32 size_px,
                           hcpp::u8 weight, bool italic) noexcept {
            const hcpp::i32 bold_shift = weight > 0 ? 1 + weight / 128 : 0;
            hcpp::i32 pen_q8 = x << 8;
            hcpp::u32 previous = 0;
            hcpp::size_t cursor = 0;
            while (cursor < text.size()) {
                const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
                if (previous != 0) {
                    pen_q8 += units_to_q8(font_kerning(previous, codepoint),
                                          size_px);
                }
                const rast::RastGlyph* glyph = rast::glyph(codepoint, size_px);
                if (glyph == nullptr) {
                    pen_q8 += fallback_advance_q8(size_px);
                    previous = codepoint;
                    continue;
                }
                if (glyph->alpha != nullptr && glyph->width > 0 &&
                    glyph->height > 0) {
                    blit_glyph(plot, *glyph, pen_q8 >> 8, baseline_y,
                               bold_shift, italic);
                }
                pen_q8 += glyph->advance_q8;
                previous = codepoint;
            }
            return q8_to_px(pen_q8 - (x << 8));
        }

        hcpp::i32 baseline_for(hcpp::i32 y, hcpp::u32 size_px) noexcept {
            return y + q8_to_px(units_to_q8(font_ascent(), size_px));
        }

        void blur_mask(hcpp::u8* mask, hcpp::i32 width, hcpp::i32 height,
                       hcpp::i32 radius) noexcept {
            if (radius <= 0) {
                return;
            }
            const hcpp::i32 window = 2 * radius + 1;
            hcpp::u8 line[k_mask_w];
            for (hcpp::i32 y = 0; y < height; ++y) {
                hcpp::u8* row = mask + y * width;
                hcpp::copy(line, row, static_cast<hcpp::size_t>(width));
                for (hcpp::i32 x = 0; x < width; ++x) {
                    hcpp::u32 sum = 0;
                    for (hcpp::i32 i = -radius; i <= radius; ++i) {
                        sum += line[clamp_i32(x + i, 0, width - 1)];
                    }
                    row[x] = static_cast<hcpp::u8>(sum / window);
                }
            }
            hcpp::u8 column_buf[k_mask_h];
            for (hcpp::i32 x = 0; x < width; ++x) {
                for (hcpp::i32 y = 0; y < height; ++y) {
                    column_buf[y] = mask[y * width + x];
                }
                for (hcpp::i32 y = 0; y < height; ++y) {
                    hcpp::u32 sum = 0;
                    for (hcpp::i32 i = -radius; i <= radius; ++i) {
                        sum += column_buf[clamp_i32(y + i, 0, height - 1)];
                    }
                    mask[y * width + x] = static_cast<hcpp::u8>(sum / window);
                }
            }
        }

        hcpp::u32 inked_pixels(hcpp::graphics::Framebuffer& fb, hcpp::u32 bg,
                               Rect region, hcpp::u32 full,
                               hcpp::u32* mid) noexcept {
            hcpp::u32 ink = 0;
            hcpp::u32 partial = 0;
            for (hcpp::i32 y = region.y; y < region.bottom(); ++y) {
                for (hcpp::i32 x = region.x; x < region.right(); ++x) {
                    const hcpp::u32 pixel = fb.get_pixel(
                        static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
                    if (pixel != bg) {
                        ++ink;
                        if (pixel != full) {
                            ++partial;
                        }
                    }
                }
            }
            if (mid != nullptr) {
                *mid = partial;
            }
            return ink;
        }

    }  // namespace

    hcpp::u32 text_scale_q8(hcpp::u32 size_px) noexcept {
        if (font_em_px() == 0 || size_px == 0) {
            return 0;
        }
        return (size_px << 8) / font_em_px();
    }

    hcpp::i32 text_measure(hcpp::StringView text, hcpp::u32 size_px) noexcept {
        // DPI scale (issue #174): every hypetext entry point multiplies
        // the caller's logical size by ui_scale_x100, so text and the
        // metrics derived from it scale together.
        size_px = hype::ui::ui_text_px(size_px);
        if (!hypefont_ready() || text_scale_q8(size_px) == 0) {
            return 0;
        }
        return q8_to_px(span_advance_q8(text, size_px));
    }

    hcpp::u32 text_line_height(hcpp::u32 size_px) noexcept {
        size_px = hype::ui::ui_text_px(size_px);
        if (text_scale_q8(size_px) == 0) {
            return 0;
        }
        const hcpp::i32 height =
            q8_to_px(units_to_q8(font_line_height(), size_px));
        return static_cast<hcpp::u32>(height > 1 ? height : 1);
    }

    hcpp::i32 text_draw_clip(hcpp::graphics::Framebuffer& fb, hcpp::i32 x,
                             hcpp::i32 y, hcpp::StringView text, hcpp::u32 color,
                             hcpp::u32 size_px, Rect clip) noexcept {
        size_px = hype::ui::ui_text_px(size_px);
        if (!hypefont_ready() || text_scale_q8(size_px) == 0 || text.empty() ||
            clip.empty()) {
            return 0;
        }
        Plot plot;
        plot.fb = &fb;
        plot.clip = clip;
        fb.decode_rgb(color, plot.red, plot.green, plot.blue);
        return draw_run(plot, x, baseline_for(y, size_px), text, size_px, 0,
                        false);
    }

    hcpp::i32 text_draw(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                        hcpp::StringView text, hcpp::u32 color,
                        hcpp::u32 size_px) noexcept {
        const Rect clip{0, 0, static_cast<hcpp::i32>(fb.width()),
                        static_cast<hcpp::i32>(fb.height())};
        return text_draw_clip(fb, x, y, text, color, size_px, clip);
    }

    hcpp::i32 text_draw_styled(hcpp::graphics::Framebuffer& fb, hcpp::i32 x,
                               hcpp::i32 y, hcpp::StringView text,
                               hcpp::u32 color, hcpp::u32 size_px,
                               hcpp::u8 weight, bool italic) noexcept {
        const Rect bounds{0, 0, static_cast<hcpp::i32>(fb.width()),
                          static_cast<hcpp::i32>(fb.height())};
        return text_draw_styled_clip(fb, x, y, text, color, size_px, weight,
                                     italic, bounds);
    }

    hcpp::i32 text_draw_styled_clip(hcpp::graphics::Framebuffer& fb, hcpp::i32 x,
                                    hcpp::i32 y, hcpp::StringView text,
                                    hcpp::u32 color, hcpp::u32 size_px,
                                    hcpp::u8 weight, bool italic,
                                    Rect clip) noexcept {
        size_px = hype::ui::ui_text_px(size_px);
        const Rect bounds{0, 0, static_cast<hcpp::i32>(fb.width()),
                          static_cast<hcpp::i32>(fb.height())};
        clip = clip.intersection(bounds);
        if (!hypefont_ready() || text_scale_q8(size_px) == 0 || text.empty() ||
            clip.empty()) {
            return 0;
        }
        Plot plot;
        plot.fb = &fb;
        plot.clip = clip;
        fb.decode_rgb(color, plot.red, plot.green, plot.blue);
        return draw_run(plot, x, baseline_for(y, size_px), text, size_px,
                        weight, italic);
    }

    hcpp::i32 text_draw_wrapped(hcpp::graphics::Framebuffer& fb, Rect rect,
                                hcpp::StringView text, TextAlign align,
                                hcpp::u32 size_px) noexcept {
        // Keep the caller's logical size for the public helper calls
        // below — they apply the DPI scale themselves (issue #174).
        const hcpp::u32 logical_px = size_px;
        size_px = hype::ui::ui_text_px(size_px);
        if (!hypefont_ready() || text_scale_q8(size_px) == 0 || rect.empty() ||
            text.empty()) {
            return 0;
        }
        Plot plot;
        plot.fb = &fb;
        plot.clip = rect;
        const hcpp::u32 white = fb.encode_rgb(255, 255, 255);
        fb.decode_rgb(white, plot.red, plot.green, plot.blue);
        const hcpp::i32 line_height =
            static_cast<hcpp::i32>(text_line_height(logical_px));
        const hcpp::i32 space_w =
            q8_to_px(rast::advance_q8(' ', size_px)) != 0
                ? q8_to_px(rast::advance_q8(' ', size_px))
                : q8_to_px(units_to_q8(
                      static_cast<hcpp::i32>(font_em_px()) / 3, size_px));
        const hcpp::size_t length = text.size();
        hcpp::size_t pos = 0;
        hcpp::i32 y = rect.y;
        hcpp::i32 used = 0;
        while (pos < length) {
            if (text[pos] == '\n') {
                ++pos;
                y += line_height;
                used += line_height;
                continue;
            }
            const hcpp::size_t line_start = pos;
            hcpp::size_t line_end = pos;
            hcpp::size_t scan = pos;
            hcpp::i32 line_w = 0;
            bool first_word = true;
            while (scan < length && text[scan] != '\n') {
                hcpp::size_t word_start = scan;
                while (word_start < length && text[word_start] == ' ') {
                    ++word_start;
                }
                if (word_start >= length || text[word_start] == '\n') {
                    line_end = word_start;
                    scan = word_start;
                    break;
                }
                hcpp::size_t word_end = word_start;
                while (word_end < length && text[word_end] != ' ' &&
                       text[word_end] != '\n') {
                    ++word_end;
                }
                const hcpp::i32 word_w = q8_to_px(span_advance_q8(
                    hcpp::StringView(text.data() + word_start,
                                     word_end - word_start),
                    size_px));
                const hcpp::i32 next_w =
                    first_word ? word_w : line_w + space_w + word_w;
                if (!first_word && next_w > rect.w) {
                    break;
                }
                line_w = next_w;
                line_end = word_end;
                scan = word_end;
                first_word = false;
            }
            if (line_end <= line_start) {
                line_end = scan > line_start ? scan : line_start + 1;
            }
            hcpp::i32 draw_x = rect.x;
            if (align == TextAlign::Center) {
                draw_x = rect.x + (rect.w - line_w) / 2;
            } else if (align == TextAlign::Right) {
                draw_x = rect.x + rect.w - line_w;
            }
            draw_run(plot, draw_x, baseline_for(y, size_px),
                     hcpp::StringView(text.data() + line_start,
                                      line_end - line_start),
                     size_px, 0, false);
            y += line_height;
            used += line_height;
            pos = line_end;
            while (pos < length && text[pos] == ' ') {
                ++pos;
            }
        }
        return used;
    }

    void text_shadow(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                     hcpp::StringView text, hcpp::u32 size_px, hcpp::u32 color,
                     hcpp::i32 shadow_offset, hcpp::i32 blur) noexcept {
        // Same pattern as text_draw_wrapped: measure helpers re-apply
        // the DPI scale, so pass the caller's logical size (issue #174).
        const hcpp::u32 logical_px = size_px;
        size_px = hype::ui::ui_text_px(size_px);
        if (!hypefont_ready() || text_scale_q8(size_px) == 0 || text.empty()) {
            return;
        }
        const hcpp::i32 text_w = text_measure(text, logical_px);
        const hcpp::i32 line_height =
            static_cast<hcpp::i32>(text_line_height(logical_px));
        hcpp::i32 pad = blur + (shadow_offset < 0 ? -shadow_offset
                                                  : shadow_offset) + 2;
        const hcpp::i32 mask_w =
            clamp_i32(text_w + 2 * pad, 1, k_mask_w);
        const hcpp::i32 mask_h =
            clamp_i32(line_height + 2 * pad, 1, k_mask_h);
        const hcpp::i32 origin_x = x - pad;
        const hcpp::i32 origin_y = y - pad;
        hcpp::set(text_mask, 0,
                  static_cast<hcpp::size_t>(mask_w) * mask_h);
        Plot mask_plot;
        mask_plot.mask = text_mask;
        mask_plot.mask_w = mask_w;
        mask_plot.mask_h = mask_h;
        mask_plot.off_x = origin_x;
        mask_plot.off_y = origin_y;
        mask_plot.clip = Rect{origin_x, origin_y, mask_w, mask_h};
        draw_run(mask_plot, x, baseline_for(y, size_px), text, size_px, 0,
                 false);
        blur_mask(text_mask, mask_w, mask_h, blur / 2);
        const hcpp::i32 sx = origin_x + shadow_offset;
        const hcpp::i32 sy = origin_y + shadow_offset;
        const Rect bounds{0, 0, static_cast<hcpp::i32>(fb.width()),
                          static_cast<hcpp::i32>(fb.height())};
        for (hcpp::i32 row = 0; row < mask_h; ++row) {
            const hcpp::i32 py = sy + row;
            if (py < 0 || py >= bounds.h) {
                continue;
            }
            for (hcpp::i32 column = 0; column < mask_w; ++column) {
                const hcpp::u32 alpha = text_mask[row * mask_w + column];
                if (alpha == 0) {
                    continue;
                }
                const hcpp::i32 px = sx + column;
                if (px < 0 || px >= bounds.w) {
                    continue;
                }
                const hcpp::u32 dst =
                    fb.get_pixel(static_cast<hcpp::u32>(px),
                                 static_cast<hcpp::u32>(py));
                hcpp::u8 red = 0;
                hcpp::u8 green = 0;
                hcpp::u8 blue = 0;
                fb.decode_rgb(dst, red, green, blue);
                const hcpp::u32 inverse = 255 - alpha;
                fb.put_pixel(static_cast<hcpp::u32>(px),
                             static_cast<hcpp::u32>(py),
                             fb.encode_rgb(
                                 static_cast<hcpp::u8>(red * inverse / 255),
                                 static_cast<hcpp::u8>(green * inverse / 255),
                                 static_cast<hcpp::u8>(blue * inverse / 255)));
            }
        }
        text_draw(fb, x, y, text, color, size_px);
    }

    bool text_selftest() noexcept {
        if (!hypefont_ready() && !font_init()) {
            return false;
        }
        static hcpp::u8 pixels[160 * 96 * 4] = {};
        hcpp::graphics::Framebuffer fb(pixels, 160, 96, 160 * 4, 32, 8, 16, 8,
                                       8, 8, 0);
        if (!fb.valid() || text_scale_q8(24) == 0) {
            return false;
        }
        const hcpp::u32 bg = fb.encode_rgb(48, 48, 48);
        const hcpp::u32 white = fb.encode_rgb(255, 255, 255);
        const Rect full{0, 0, 160, 96};

        fb.clear(bg);
        const hcpp::i32 drawn = text_draw(fb, 4, 4, "HypeAI", white, 24);
        hcpp::u32 mid = 0;
        const hcpp::u32 ink = inked_pixels(fb, bg, full, white, &mid);
        text_shadow(fb, 4, 40, "HypeAI", 24, white, 3, 4);
        const hcpp::u32 shadow_ink = inked_pixels(fb, bg, full, white, nullptr);
        const hcpp::i32 expected = text_measure("HypeAI", 24);
        const bool ttf_ok = ink > 0 && mid > 0 && drawn == expected &&
                            expected > 0 && shadow_ink > ink;
        serial::write_line(ttf_ok ? "text ttf=ok" : "text ttf=fail");

        fb.clear(bg);
        const hcpp::i32 line_height =
            static_cast<hcpp::i32>(text_line_height(16));
        const hcpp::i32 used = text_draw_wrapped(
            fb, Rect{4, 4, 56, 88}, "alpha beta gamma delta", TextAlign::Left,
            16);
        const hcpp::u32 wrapped = inked_pixels(
            fb, bg, Rect{4, 4 + line_height, 56, 88 - line_height}, white,
            nullptr);
        const bool wrap_ok = used >= 2 * line_height && wrapped > 0;
        serial::write_line(wrap_ok ? "text wrap=ok" : "text wrap=fail");

        fb.clear(bg);
        text_draw(fb, 4, 4, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
                  white, 24);
        const hcpp::u32 cyr_ink = inked_pixels(fb, bg, full, white, nullptr);
        const bool cyr_ok = cyr_ink > 0;
        serial::write_line(cyr_ok ? "text cyr=ok" : "text cyr=fail");

        return ttf_ok && wrap_ok && cyr_ok;
    }

}  // namespace hype::font
