#include "kernel/font/hypefont.h"

#include "hcpp/memory.hpp"
#include "hcpp/utf8.hpp"
#include "kernel/codecapps/image/inflate.h"
#include "kernel/font/ttf.h"
#include "kernel/font/ttfrast.h"

namespace hype::font {

    namespace {

        constexpr hcpp::size_t ttf_buffer_cap = 1024 * 1024;
        hcpp::u8 ttf_buffer[ttf_buffer_cap];
        bool loaded = false;

    }  // namespace

    bool FontGlyphMetric::valid() const noexcept { return glyph_index != 0; }

    bool hypefont_load(const hcpp::u8* data, hcpp::size_t size) noexcept {
        loaded = ttf::parse(data, size);
        rast::reset();
        return loaded;
    }

    bool font_init() noexcept {
        hcpp::size_t produced = 0;
        if (!codec::image::inflate_zlib(montserrat_ttf_zlib,
                                      montserrat_ttf_zlib_size, ttf_buffer,
                                      sizeof(ttf_buffer), produced) ||
            produced != montserrat_ttf_size) {
            loaded = false;
            return false;
        }
        return hypefont_load(ttf_buffer, produced);
    }

    bool hypefont_ready() noexcept { return loaded; }

    FontGlyphMetric font_glyph(hcpp::u32 codepoint) noexcept {
        FontGlyphMetric metric;
        metric.codepoint = codepoint;
        if (!loaded || codepoint == 0 || codepoint > 0x10FFFF) {
            return metric;
        }
        const hcpp::u16 index = ttf::glyph_index(codepoint);
        if (index == 0) {
            return metric;
        }
        metric.glyph_index = index;
        metric.advance = ttf::advance(index);
        hcpp::i16 x_min = 0;
        hcpp::i16 y_min = 0;
        hcpp::i16 x_max = 0;
        hcpp::i16 y_max = 0;
        if (ttf::glyph_box(index, x_min, y_min, x_max, y_max)) {
            metric.bearing_x = x_min;
            metric.bearing_y = y_max;
            metric.width = x_max - x_min;
            metric.height = y_max - y_min;
        }
        return metric;
    }

    hcpp::u32 font_glyph_count() noexcept {
        return loaded ? ttf::glyph_count() : 0;
    }

    hcpp::u32 font_em_px() noexcept { return ttf::units_per_em(); }

    hcpp::i32 font_ascent() noexcept { return ttf::ascent(); }

    hcpp::i32 font_descent() noexcept { return ttf::descent(); }

    hcpp::i32 font_line_height() noexcept {
        return ttf::ascent() - ttf::descent() + ttf::line_gap();
    }

    hcpp::i32 font_kerning(hcpp::u32 left, hcpp::u32 right) noexcept {
        if (!loaded) {
            return 0;
        }
        const hcpp::u16 left_index = ttf::glyph_index(left);
        const hcpp::u16 right_index = ttf::glyph_index(right);
        if (left_index == 0 || right_index == 0) {
            return 0;
        }
        return ttf::kerning(left_index, right_index);
    }

    hcpp::i32 font_measure(hcpp::StringView text) noexcept {
        if (!loaded) {
            return 0;
        }
        hcpp::i32 width = 0;
        hcpp::u32 previous = 0;
        hcpp::size_t cursor = 0;
        while (cursor < text.size()) {
            const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
            if (previous != 0) {
                width += font_kerning(previous, codepoint);
            }
            const FontGlyphMetric glyph = font_glyph(codepoint);
            if (glyph.valid()) {
                width += glyph.advance;
            } else {
                const FontGlyphMetric fallback = font_glyph('?');
                width += fallback.valid() ? fallback.advance
                                        : font_em_px() / 2;
            }
            previous = codepoint;
        }
        return width;
    }

    bool font_selftest() noexcept {
        if (!loaded && !font_init()) {
            return false;
        }
        const FontGlyphMetric glyph_h = font_glyph('H');
        const FontGlyphMetric glyph_space = font_glyph(' ');
        if (!glyph_h.valid() || glyph_h.advance <= 0 || !glyph_space.valid() ||
            glyph_space.advance <= 0) {
            return false;
        }
        hcpp::i32 expected = 0;
        for (const char* p = "HypeAI"; *p != '\0'; ++p) {
            expected += font_glyph(static_cast<hcpp::u32>(*p)).advance;
        }
        const hcpp::i32 measured = font_measure("HypeAI");
        if (expected <= 0 || measured <= 0 || measured > expected) {
            return false;
        }
        const FontGlyphMetric cyrillic = font_glyph(0x041F);
        if (!cyrillic.valid() || font_glyph(0xFFFE).valid() ||
            font_glyph(0).valid()) {
            return false;
        }
        const rast::RastGlyph* inked = rast::glyph('H', 24);
        if (inked == nullptr || inked->alpha == nullptr || inked->width <= 0 ||
            inked->height <= 0 || inked->advance_q8 <= 0) {
            return false;
        }
        hcpp::u32 ink = 0;
        const hcpp::u32 pixels =
            static_cast<hcpp::u32>(inked->width) * inked->height;
        for (hcpp::u32 i = 0; i < pixels; ++i) {
            if (inked->alpha[i] > 128) {
                ++ink;
            }
        }
        if (ink == 0 || ink >= pixels) {
            return false;
        }
        const rast::RastGlyph* cyr = rast::glyph(0x041F, 20);
        if (cyr == nullptr || cyr->alpha == nullptr || cyr->width == 0) {
            return false;
        }
        hcpp::u8 garbage[64];
        hcpp::copy(garbage, ttf_buffer, sizeof(garbage));
        garbage[0] = 'X';
        if (hypefont_load(garbage, sizeof(garbage))) {
            font_init();
            return false;
        }
        return font_init();
    }

}  // namespace hype::font
