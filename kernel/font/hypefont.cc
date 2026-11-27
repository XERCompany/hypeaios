#include "kernel/font/hypefont.h"

#include "hcpp/memory.hpp"
#include "hcpp/utf8.hpp"

namespace hype::font {

    namespace {

        constexpr hcpp::size_t hfnt_header_size = 32;
        constexpr hcpp::size_t hfnt_record_size = 16;
        constexpr hcpp::size_t hfnt_kern_size = 12;
        constexpr hcpp::u16 hfnt_version = 1;
        constexpr hcpp::size_t hfnt_version_offset = 4;
        constexpr hcpp::size_t hfnt_flags_offset = 6;
        constexpr hcpp::size_t hfnt_glyph_count_offset = 8;
        constexpr hcpp::size_t hfnt_atlas_width_offset = 12;
        constexpr hcpp::size_t hfnt_atlas_height_offset = 16;
        constexpr hcpp::size_t hfnt_atlas_offset_offset = 20;
        constexpr hcpp::size_t hfnt_spread_offset = 24;
        constexpr hcpp::size_t hfnt_px_size_offset = 25;
        constexpr hcpp::size_t hfnt_line_height_offset = 26;
        constexpr hcpp::size_t hfnt_kern_count_offset = 28;
        constexpr hcpp::size_t hfnt_rec_x = 4;
        constexpr hcpp::size_t hfnt_rec_y = 6;
        constexpr hcpp::size_t hfnt_rec_width = 8;
        constexpr hcpp::size_t hfnt_rec_height = 10;
        constexpr hcpp::size_t hfnt_rec_advance = 12;
        constexpr hcpp::size_t hfnt_rec_bearing_x = 14;
        constexpr hcpp::size_t hfnt_rec_bearing_y = 15;

        struct HfntBlob {
            const hcpp::u8* data = nullptr;
            hcpp::size_t size = 0;
            hcpp::u32 glyph_count = 0;
            hcpp::u32 atlas_width = 0;
            hcpp::u32 atlas_height = 0;
            hcpp::u32 atlas_offset = 0;
            hcpp::u32 kern_count = 0;
            hcpp::u16 line_height = 0;
            hcpp::u8 spread = 0;
            hcpp::u8 px_size = 0;
        };

        HfntBlob loaded;

        hcpp::u16 read_u16(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::u16>(at[0]) | (static_cast<hcpp::u16>(at[1]) << 8);
        }

        hcpp::u32 read_u32(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::u32>(at[0]) | (static_cast<hcpp::u32>(at[1]) << 8) |
                   (static_cast<hcpp::u32>(at[2]) << 16) | (static_cast<hcpp::u32>(at[3]) << 24);
        }

        hcpp::i16 read_i16(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::i16>(read_u16(at));
        }

        const hcpp::u8* glyph_table() noexcept { return loaded.data + hfnt_header_size; }

        const hcpp::u8* kern_table() noexcept {
            return loaded.data + hfnt_header_size + loaded.glyph_count * hfnt_record_size;
        }

        bool find_glyph(hcpp::u32 codepoint, FontGlyphSdf& out) noexcept {
            const hcpp::u8* table = glyph_table();
            hcpp::u32 low = 0;
            hcpp::u32 high = loaded.glyph_count;
            while (low < high) {
                const hcpp::u32 mid = low + (high - low) / 2;
                if (read_u32(table + mid * hfnt_record_size) < codepoint) {
                    low = mid + 1;
                } else {
                    high = mid;
                }
            }
            if (low >= loaded.glyph_count ||
                read_u32(table + low * hfnt_record_size) != codepoint) {
                return false;
            }
            const hcpp::u8* record = table + low * hfnt_record_size;
            out.codepoint = codepoint;
            out.x = read_u16(record + hfnt_rec_x);
            out.y = read_u16(record + hfnt_rec_y);
            out.width = read_u16(record + hfnt_rec_width);
            out.height = read_u16(record + hfnt_rec_height);
            out.advance = read_i16(record + hfnt_rec_advance);
            out.bearing_x = static_cast<hcpp::i8>(record[hfnt_rec_bearing_x]);
            out.bearing_y = static_cast<hcpp::i8>(record[hfnt_rec_bearing_y]);
            return true;
        }

    }  // namespace

    bool FontGlyphSdf::valid() const noexcept { return codepoint != 0; }

    bool hypefont_load(const hcpp::u8* data, hcpp::size_t size) noexcept {
        if (data == nullptr || size < hfnt_header_size) {
            return false;
        }
        if (read_u32(data) != hfnt_magic ||
            read_u16(data + hfnt_version_offset) != hfnt_version ||
            read_u16(data + hfnt_flags_offset) != 0) {
            return false;
        }
        const hcpp::u32 glyph_count = read_u32(data + hfnt_glyph_count_offset);
        const hcpp::u32 atlas_width = read_u32(data + hfnt_atlas_width_offset);
        const hcpp::u32 atlas_height = read_u32(data + hfnt_atlas_height_offset);
        const hcpp::u32 atlas_offset = read_u32(data + hfnt_atlas_offset_offset);
        const hcpp::u32 kern_count = read_u32(data + hfnt_kern_count_offset);
        const hcpp::u16 line_height = read_u16(data + hfnt_line_height_offset);
        const hcpp::u8 spread = data[hfnt_spread_offset];
        const hcpp::u8 px_size = data[hfnt_px_size_offset];
        if (glyph_count == 0 || glyph_count > hfnt_max_glyphs ||
            atlas_width == 0 || atlas_height == 0 ||
            atlas_width > hfnt_max_atlas || atlas_height > hfnt_max_atlas ||
            line_height == 0 || spread == 0 || px_size == 0) {
            return false;
        }
        const hcpp::u64 glyph_table_end =
            static_cast<hcpp::u64>(hfnt_header_size) +
            static_cast<hcpp::u64>(glyph_count) * hfnt_record_size;
        const hcpp::u64 kern_end = glyph_table_end +
                                   static_cast<hcpp::u64>(kern_count) * hfnt_kern_size;
        if (kern_end > size || atlas_offset < kern_end ||
            static_cast<hcpp::u64>(atlas_offset) +
                    static_cast<hcpp::u64>(atlas_width) * atlas_height >
                size) {
            return false;
        }
        const hcpp::u8* table = data + hfnt_header_size;
        hcpp::u32 previous = 0;
        for (hcpp::u32 index = 0; index < glyph_count; ++index) {
            const hcpp::u8* record = table + index * hfnt_record_size;
            const hcpp::u32 codepoint = read_u32(record);
            const hcpp::u32 x = read_u16(record + hfnt_rec_x);
            const hcpp::u32 y = read_u16(record + hfnt_rec_y);
            const hcpp::u32 width = read_u16(record + hfnt_rec_width);
            const hcpp::u32 height = read_u16(record + hfnt_rec_height);
            if (x + width > atlas_width || y + height > atlas_height ||
                (width == 0) != (height == 0) ||
                (width == 0 && x != 0)) {
                return false;
            }
            if (index != 0 && codepoint <= previous) {
                return false;
            }
            previous = codepoint;
        }
        loaded.data = data;
        loaded.size = size;
        loaded.glyph_count = glyph_count;
        loaded.atlas_width = atlas_width;
        loaded.atlas_height = atlas_height;
        loaded.atlas_offset = atlas_offset;
        loaded.kern_count = kern_count;
        loaded.line_height = line_height;
        loaded.spread = spread;
        loaded.px_size = px_size;
        return true;
    }

    bool hypefont_ready() noexcept { return loaded.data != nullptr; }

    FontGlyphSdf font_glyph(hcpp::u32 codepoint) noexcept {
        FontGlyphSdf glyph;
        if (!hypefont_ready() || codepoint == 0 || codepoint > 0x10FFFF) {
            return glyph;
        }
        find_glyph(codepoint, glyph);
        return glyph;
    }

    const hcpp::u8* font_atlas() noexcept {
        return hypefont_ready() ? loaded.data + loaded.atlas_offset : nullptr;
    }

    hcpp::u32 font_atlas_width() noexcept { return loaded.atlas_width; }

    hcpp::u32 font_atlas_height() noexcept { return loaded.atlas_height; }

    hcpp::u32 font_glyph_count() noexcept { return loaded.glyph_count; }

    hcpp::u32 font_spread() noexcept { return loaded.spread; }

    hcpp::u32 font_em_px() noexcept { return loaded.px_size; }

    hcpp::u16 font_line_height() noexcept { return loaded.line_height; }

    hcpp::i32 font_kerning(hcpp::u32 left, hcpp::u32 right) noexcept {
        if (!hypefont_ready() || loaded.kern_count == 0) {
            return 0;
        }
        const hcpp::u8* table = kern_table();
        hcpp::u32 low = 0;
        hcpp::u32 high = loaded.kern_count;
        while (low < high) {
            const hcpp::u32 mid = low + (high - low) / 2;
            const hcpp::u8* pair = table + mid * hfnt_kern_size;
            const hcpp::u32 pair_left = read_u32(pair);
            const hcpp::u32 pair_right = read_u32(pair + 4);
            if (pair_left < left || (pair_left == left && pair_right < right)) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (low >= loaded.kern_count) {
            return 0;
        }
        const hcpp::u8* pair = table + low * hfnt_kern_size;
        if (read_u32(pair) != left || read_u32(pair + 4) != right) {
            return 0;
        }
        return static_cast<hcpp::i32>(read_u32(pair + 8));
    }

    hcpp::i32 font_measure(hcpp::StringView text) noexcept {
        if (!hypefont_ready()) {
            return 0;
        }
        hcpp::i32 width = 0;
        hcpp::u32 previous = 0;
        hcpp::size_t cursor = 0;
        while (cursor < text.size()) {
            const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
            const FontGlyphSdf glyph = font_glyph(codepoint);
            if (previous != 0) {
                width += font_kerning(previous, codepoint);
            }
            if (glyph.valid()) {
                width += glyph.advance;
            } else {
                const FontGlyphSdf fallback = font_glyph('?');
                width += fallback.valid() ? fallback.advance : loaded.px_size / 2;
            }
            previous = codepoint;
        }
        return width;
    }

    bool font_selftest() noexcept {
        if (!hypefont_load(montserrat_sdf_hfnt, montserrat_sdf_hfnt_size)) {
            return false;
        }
        const hcpp::u8* atlas = font_atlas();
        if (atlas == nullptr || font_atlas_width() < 8 || font_atlas_height() < 8) {
            return false;
        }
        hcpp::u64 ink = 0;
        hcpp::u64 outside = 0;
        const hcpp::u64 atlas_size =
            static_cast<hcpp::u64>(loaded.atlas_width) * loaded.atlas_height;
        for (hcpp::u64 index = 0; index < atlas_size; ++index) {
            if (atlas[index] > 128) {
                ++ink;
            } else if (atlas[index] < 128) {
                ++outside;
            }
        }
        if (ink == 0 || outside == 0 || ink >= atlas_size / 2) {
            return false;
        }
        const FontGlyphSdf glyph_h = font_glyph('H');
        const FontGlyphSdf glyph_space = font_glyph(' ');
        if (!glyph_h.valid() || glyph_h.width == 0 || glyph_h.advance <= 0 ||
            !glyph_space.valid() || glyph_space.advance <= 0) {
            return false;
        }
        hcpp::i32 expected = 0;
        for (const char* p = "HypeAI"; *p != '\0'; ++p) {
            expected += font_glyph(static_cast<hcpp::u32>(*p)).advance;
        }
        if (expected <= 0 || font_measure("HypeAI") != expected) {
            return false;
        }
        const FontGlyphSdf cyrillic = font_glyph(0x041F);
        if (!cyrillic.valid() || cyrillic.width == 0 ||
            font_glyph(0xFFFE).valid() || font_glyph(0).valid()) {
            return false;
        }
        hcpp::u8 bad_header[hfnt_header_size];
        hcpp::copy(bad_header, montserrat_sdf_hfnt, hfnt_header_size);
        bad_header[0] = 'X';
        if (hypefont_load(bad_header, sizeof(bad_header))) {
            hypefont_load(montserrat_sdf_hfnt, montserrat_sdf_hfnt_size);
            return false;
        }
        hypefont_load(montserrat_sdf_hfnt, montserrat_sdf_hfnt_size);
        return hypefont_ready();
    }

}  // namespace hype::font
