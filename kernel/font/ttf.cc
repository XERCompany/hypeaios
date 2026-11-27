#include "kernel/font/ttf.h"

#include "hcpp/memory.hpp"

namespace hype::font::ttf {

    namespace {

        constexpr hcpp::u32 sfnt_version = 0x00010000;
        constexpr hcpp::u32 sfnt_version_true = 0x74727565;
        constexpr hcpp::u32 max_tables = 200;
        constexpr hcpp::u32 max_compound_depth = 4;
        constexpr hcpp::u16 flag_on_curve = 0x01;
        constexpr hcpp::u16 flag_x_short = 0x02;
        constexpr hcpp::u16 flag_y_short = 0x04;
        constexpr hcpp::u16 flag_repeat = 0x08;
        constexpr hcpp::u16 flag_x_same = 0x10;
        constexpr hcpp::u16 flag_y_same = 0x20;
        constexpr hcpp::u16 comp_arg_words = 0x0001;
        constexpr hcpp::u16 comp_args_xy = 0x0002;
        constexpr hcpp::u16 comp_scale = 0x0008;
        constexpr hcpp::u16 comp_more = 0x0020;
        constexpr hcpp::u16 comp_scale_xy = 0x0040;
        constexpr hcpp::u16 comp_scale_2x2 = 0x0080;

        struct Face {
            const hcpp::u8* data = nullptr;
            hcpp::size_t size = 0;
            hcpp::u32 cmap = 0;
            hcpp::u32 cmap_len = 0;
            hcpp::u32 cmap_sub = 0;
            hcpp::u32 glyf = 0;
            hcpp::u32 glyf_len = 0;
            hcpp::u32 hmtx = 0;
            hcpp::u32 kern = 0;
            hcpp::u32 loca = 0;
            hcpp::u32 num_glyphs = 0;
            hcpp::u16 units_per_em = 0;
            hcpp::i16 asc = 0;
            hcpp::i16 desc = 0;
            hcpp::i16 gap = 0;
            hcpp::u16 num_hmetrics = 0;
            hcpp::u16 cmap_fmt = 0;
            hcpp::i16 loca_long = 0;
        };

        Face face;
        TtfOutline scratch[max_compound_depth + 1];
        TtfOutline empty_outline;

        hcpp::u16 ru16(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::u16>(at[0] << 8 | at[1]);
        }

        hcpp::i16 ri16(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::i16>(ru16(at));
        }

        hcpp::u32 ru32(const hcpp::u8* at) noexcept {
            return static_cast<hcpp::u32>(at[0]) << 24 |
                   static_cast<hcpp::u32>(at[1]) << 16 |
                   static_cast<hcpp::u32>(at[2]) << 8 |
                   static_cast<hcpp::u32>(at[3]);
        }

        bool find_table(hcpp::u32 tag, hcpp::u32& offset,
                        hcpp::u32& length) noexcept {
            const hcpp::u16 count = ru16(face.data + 4);
            for (hcpp::u32 index = 0; index < count; ++index) {
                const hcpp::u8* record = face.data + 12 + index * 16;
                if (ru32(record) != tag) {
                    continue;
                }
                const hcpp::u32 off = ru32(record + 8);
                const hcpp::u32 len = ru32(record + 12);
                if (static_cast<hcpp::u64>(off) + len > face.size) {
                    return false;
                }
                offset = off;
                length = len;
                return true;
            }
            return false;
        }

        constexpr hcpp::u32 tag_of(char a, char b, char c, char d) noexcept {
            return static_cast<hcpp::u32>(static_cast<hcpp::u8>(a)) << 24 |
                   static_cast<hcpp::u32>(static_cast<hcpp::u8>(b)) << 16 |
                   static_cast<hcpp::u32>(static_cast<hcpp::u8>(c)) << 8 |
                   static_cast<hcpp::u32>(static_cast<hcpp::u8>(d));
        }

        bool pick_cmap() noexcept {
            const hcpp::u8* cmap = face.data + face.cmap;
            if (face.cmap_len < 4 || ru16(cmap) != 0) {
                return false;
            }
            const hcpp::u16 count = ru16(cmap + 2);
            if (4 + static_cast<hcpp::u32>(count) * 8 > face.cmap_len) {
                return false;
            }
            hcpp::u32 fmt12 = 0;
            hcpp::u32 fmt4 = 0;
            for (hcpp::u32 index = 0; index < count; ++index) {
                const hcpp::u8* record = cmap + 4 + index * 8;
                const hcpp::u32 sub = ru32(record + 4);
                if (sub + 2 > face.cmap_len) {
                    continue;
                }
                const hcpp::u16 fmt = ru16(cmap + sub);
                if (fmt == 12 && fmt12 == 0 && sub + 16 <= face.cmap_len) {
                    fmt12 = sub;
                } else if (fmt == 4 && fmt4 == 0 && sub + 16 <= face.cmap_len) {
                    fmt4 = sub;
                }
            }
            if (fmt12 != 0) {
                face.cmap_sub = fmt12;
                face.cmap_fmt = 12;
                return true;
            }
            if (fmt4 != 0) {
                face.cmap_sub = fmt4;
                face.cmap_fmt = 4;
                return true;
            }
            return false;
        }

        hcpp::u16 cmap_fmt4(hcpp::u32 codepoint) noexcept {
            const hcpp::u8* sub = face.data + face.cmap + face.cmap_sub;
            if (codepoint > 0xFFFF) {
                return 0;
            }
            const hcpp::u16 seg_count = ru16(sub + 6) / 2;
            const hcpp::u8* ends = sub + 14;
            const hcpp::u8* starts = ends + seg_count * 2 + 2;
            const hcpp::u8* deltas = starts + seg_count * 2;
            const hcpp::u8* roffs = deltas + seg_count * 2;
            hcpp::u32 low = 0;
            hcpp::u32 high = seg_count;
            while (low < high) {
                const hcpp::u32 mid = low + (high - low) / 2;
                if (ru16(ends + mid * 2) < codepoint) {
                    low = mid + 1;
                } else {
                    high = mid;
                }
            }
            if (low >= seg_count ||
                codepoint < ru16(starts + low * 2)) {
                return 0;
            }
            const hcpp::i16 delta = ri16(deltas + low * 2);
            const hcpp::u16 roff = ru16(roffs + low * 2);
            if (roff == 0) {
                return static_cast<hcpp::u16>(
                    (static_cast<hcpp::i32>(codepoint) + delta) & 0xFFFF);
            }
            const hcpp::u8* at = roffs + low * 2 + roff +
                                 (codepoint - ru16(starts + low * 2)) * 2;
            if (at + 2 > face.data + face.size ||
                at < face.data + face.cmap + face.cmap_sub) {
                return 0;
            }
            hcpp::u16 glyph = ru16(at);
            if (glyph != 0) {
                glyph = static_cast<hcpp::u16>(
                    (static_cast<hcpp::i32>(glyph) + delta) & 0xFFFF);
            }
            return glyph;
        }

        hcpp::u16 cmap_fmt12(hcpp::u32 codepoint) noexcept {
            const hcpp::u8* sub = face.data + face.cmap + face.cmap_sub;
            const hcpp::u32 groups = ru32(sub + 12);
            if (16 + static_cast<hcpp::u64>(groups) * 12 > face.cmap_len ||
                groups == 0) {
                return 0;
            }
            const hcpp::u8* table = sub + 16;
            hcpp::u32 low = 0;
            hcpp::u32 high = groups;
            while (low < high) {
                const hcpp::u32 mid = low + (high - low) / 2;
                if (ru32(table + mid * 12 + 4) < codepoint) {
                    low = mid + 1;
                } else {
                    high = mid;
                }
            }
            if (low >= groups ||
                codepoint < ru32(table + low * 12)) {
                return 0;
            }
            const hcpp::u64 glyph =
                static_cast<hcpp::u64>(ru32(table + low * 12 + 8)) +
                (codepoint - ru32(table + low * 12));
            return glyph < face.num_glyphs && glyph < 0x10000
                       ? static_cast<hcpp::u16>(glyph)
                       : 0;
        }

        hcpp::u32 glyph_offset(hcpp::u16 glyph) noexcept {
            const hcpp::u8* loca = face.data + face.loca;
            if (face.loca_long) {
                return ru32(loca + static_cast<hcpp::u32>(glyph) * 4);
            }
            return static_cast<hcpp::u32>(
                       ru16(loca + static_cast<hcpp::u32>(glyph) * 2)) *
                   2;
        }

        bool append_point(TtfOutline* out, hcpp::i32 x, hcpp::i32 y,
                          bool on) noexcept {
            if (out->point_count >= ttf_max_points) {
                return false;
            }
            const hcpp::u32 index = out->point_count++;
            out->x[index] = x > 32767 ? 32767 : x < -32768 ? -32768
                                                         : static_cast<hcpp::i16>(x);
            out->y[index] = y > 32767 ? 32767 : y < -32768 ? -32768
                                                         : static_cast<hcpp::i16>(y);
            out->on_curve[index] = on ? 1 : 0;
            return true;
        }

        bool load_simple(const hcpp::u8* g, hcpp::u32 length, hcpp::u16 contours,
                         TtfOutline* out) noexcept {
            const hcpp::u8* ends = g + 10;
            if (10 + static_cast<hcpp::u32>(contours) * 2 + 2 > length) {
                return false;
            }
            const hcpp::u16 point_total = ru16(ends + (contours - 1) * 2) + 1;
            if (point_total > ttf_max_points) {
                return false;
            }
            const hcpp::u16 instr_len = ru16(ends + contours * 2);
            const hcpp::u8* at = ends + contours * 2 + 2 + instr_len;
            if (at > g + length) {
                return false;
            }
            hcpp::u16 flags[ttf_max_points];
            hcpp::u32 filled = 0;
            while (filled < point_total) {
                if (at >= g + length) {
                    return false;
                }
                const hcpp::u8 flag = *at++;
                hcpp::u32 run = 1;
                if (flag & flag_repeat) {
                    if (at >= g + length) {
                        return false;
                    }
                    run += *at++;
                }
                if (filled + run > point_total) {
                    return false;
                }
                for (hcpp::u32 i = 0; i < run; ++i) {
                    flags[filled + i] = flag;
                }
                filled += run;
            }
            hcpp::i32 px = 0;
            for (hcpp::u32 i = 0; i < point_total; ++i) {
                hcpp::i32 dx = 0;
                if (flags[i] & flag_x_short) {
                    if (at >= g + length) {
                        return false;
                    }
                    dx = *at++;
                    if (!(flags[i] & flag_x_same)) {
                        dx = -dx;
                    }
                } else if (!(flags[i] & flag_x_same)) {
                    if (at + 2 > g + length) {
                        return false;
                    }
                    dx = ri16(at);
                    at += 2;
                }
                px += dx;
                out->x[i] = static_cast<hcpp::i16>(px);
            }
            hcpp::i32 py = 0;
            for (hcpp::u32 i = 0; i < point_total; ++i) {
                hcpp::i32 dy = 0;
                if (flags[i] & flag_y_short) {
                    if (at >= g + length) {
                        return false;
                    }
                    dy = *at++;
                    if (!(flags[i] & flag_y_same)) {
                        dy = -dy;
                    }
                } else if (!(flags[i] & flag_y_same)) {
                    if (at + 2 > g + length) {
                        return false;
                    }
                    dy = ri16(at);
                    at += 2;
                }
                py += dy;
                out->y[i] = static_cast<hcpp::i16>(py);
                out->on_curve[i] = flags[i] & flag_on_curve ? 1 : 0;
            }
            for (hcpp::u32 c = 0; c < contours; ++c) {
                out->contour_end[c] = ru16(ends + c * 2);
            }
            out->point_count = static_cast<hcpp::u16>(point_total);
            out->contour_count = contours;
            return true;
        }

        bool load_glyph(hcpp::u16 glyph, hcpp::u32 depth,
                        TtfOutline* out) noexcept {
            if (depth > max_compound_depth || glyph >= face.num_glyphs) {
                return false;
            }
            const hcpp::u32 begin = glyph_offset(glyph);
            const hcpp::u32 end = glyph_offset(glyph + 1);
            if (end <= begin) {
                return true;
            }
            if (end > face.glyf_len || end - begin < 10) {
                return false;
            }
            const hcpp::u8* g = face.data + face.glyf + begin;
            const hcpp::u32 length = end - begin;
            out->x_min = ri16(g + 2);
            out->y_min = ri16(g + 4);
            out->x_max = ri16(g + 6);
            out->y_max = ri16(g + 8);
            const hcpp::i16 contours = ri16(g);
            if (contours >= 0) {
                if (contours == 0 ||
                    contours > static_cast<hcpp::i16>(ttf_max_contours)) {
                    return contours == 0;
                }
                return load_simple(g, length, static_cast<hcpp::u16>(contours),
                                   out);
            }
            const hcpp::u8* at = g + 10;
            const hcpp::u8* limit = g + length;
            hcpp::u16 flags = 0;
            do {
                if (at + 4 > limit) {
                    return false;
                }
                flags = ru16(at);
                const hcpp::u16 component = ru16(at + 2);
                at += 4;
                hcpp::i32 dx = 0;
                hcpp::i32 dy = 0;
                if (flags & comp_arg_words) {
                    if (at + 4 > limit) {
                        return false;
                    }
                    dx = ri16(at);
                    dy = ri16(at + 2);
                    at += 4;
                } else {
                    if (at + 2 > limit) {
                        return false;
                    }
                    dx = static_cast<hcpp::i8>(at[0]);
                    dy = static_cast<hcpp::i8>(at[1]);
                    at += 2;
                }
                if (!(flags & comp_args_xy)) {
                    return false;
                }
                hcpp::i32 m00 = 16384;
                hcpp::i32 m01 = 0;
                hcpp::i32 m10 = 0;
                hcpp::i32 m11 = 16384;
                if (flags & comp_scale) {
                    if (at + 2 > limit) {
                        return false;
                    }
                    m00 = m11 = ri16(at);
                    at += 2;
                } else if (flags & comp_scale_xy) {
                    if (at + 4 > limit) {
                        return false;
                    }
                    m00 = ri16(at);
                    m11 = ri16(at + 2);
                    at += 4;
                } else if (flags & comp_scale_2x2) {
                    if (at + 8 > limit) {
                        return false;
                    }
                    m00 = ri16(at);
                    m01 = ri16(at + 2);
                    m10 = ri16(at + 4);
                    m11 = ri16(at + 6);
                    at += 8;
                }
                TtfOutline* child = &scratch[depth];
                *child = empty_outline;
                if (!load_glyph(component, depth + 1, child)) {
                    return false;
                }
                if (static_cast<hcpp::u32>(out->point_count) +
                            child->point_count >
                        ttf_max_points ||
                    static_cast<hcpp::u32>(out->contour_count) +
                            child->contour_count >
                        ttf_max_contours) {
                    return false;
                }
                const hcpp::u16 base = out->point_count;
                for (hcpp::u32 i = 0; i < child->point_count; ++i) {
                    const hcpp::i32 cx = child->x[i];
                    const hcpp::i32 cy = child->y[i];
                    const hcpp::i32 tx =
                        (cx * m00 + cy * m01 + 8192) / 16384 + dx;
                    const hcpp::i32 ty =
                        (cx * m10 + cy * m11 + 8192) / 16384 + dy;
                    if (!append_point(out, tx, ty, child->on_curve[i] != 0)) {
                        return false;
                    }
                }
                for (hcpp::u32 c = 0; c < child->contour_count; ++c) {
                    out->contour_end[out->contour_count + c] =
                        static_cast<hcpp::u16>(base + child->contour_end[c]);
                }
                out->contour_count =
                    static_cast<hcpp::u16>(out->contour_count +
                                           child->contour_count);
            } while (flags & comp_more);
            return true;
        }

    }  // namespace

    bool parse(const hcpp::u8* data, hcpp::size_t size) noexcept {
        face = Face{};
        if (data == nullptr || size < 12) {
            return false;
        }
        const hcpp::u32 version = ru32(data);
        if (version != sfnt_version && version != sfnt_version_true) {
            return false;
        }
        const hcpp::u16 count = ru16(data + 4);
        if (count == 0 || count > max_tables ||
            12 + static_cast<hcpp::u64>(count) * 16 > size) {
            return false;
        }
        face.data = data;
        face.size = size;
        hcpp::u32 head = 0;
        hcpp::u32 head_len = 0;
        hcpp::u32 hhea = 0;
        hcpp::u32 hhea_len = 0;
        hcpp::u32 hmtx_len = 0;
        hcpp::u32 loca_len = 0;
        hcpp::u32 maxp = 0;
        hcpp::u32 maxp_len = 0;
        hcpp::u32 kern_len = 0;
        if (!find_table(tag_of('h', 'e', 'a', 'd'), head, head_len) ||
            !find_table(tag_of('h', 'h', 'e', 'a'), hhea, hhea_len) ||
            !find_table(tag_of('h', 'm', 't', 'x'), face.hmtx, hmtx_len) ||
            !find_table(tag_of('l', 'o', 'c', 'a'), face.loca, loca_len) ||
            !find_table(tag_of('g', 'l', 'y', 'f'), face.glyf,
                        face.glyf_len) ||
            !find_table(tag_of('m', 'a', 'x', 'p'), maxp, maxp_len) ||
            !find_table(tag_of('c', 'm', 'a', 'p'), face.cmap,
                        face.cmap_len)) {
            face.data = nullptr;
            return false;
        }
        find_table(tag_of('k', 'e', 'r', 'n'), face.kern, kern_len);
        if (head_len < 54 || hhea_len < 36 || maxp_len < 6) {
            face.data = nullptr;
            return false;
        }
        const hcpp::u8* head_p = data + head;
        const hcpp::u8* hhea_p = data + hhea;
        const hcpp::u8* maxp_p = data + maxp;
        face.units_per_em = ru16(head_p + 18);
        face.loca_long = ri16(head_p + 50);
        face.asc = ri16(hhea_p + 4);
        face.desc = ri16(hhea_p + 6);
        face.gap = ri16(hhea_p + 8);
        face.num_hmetrics = ru16(hhea_p + 34);
        face.num_glyphs = ru16(maxp_p + 4);
        if (face.units_per_em < 16 || face.units_per_em > 16384 ||
            face.num_glyphs == 0 || face.num_hmetrics == 0 ||
            face.num_hmetrics > face.num_glyphs ||
            (face.loca_long != 0 && face.loca_long != 1)) {
            face.data = nullptr;
            return false;
        }
        const hcpp::u64 hmtx_need =
            static_cast<hcpp::u64>(face.num_hmetrics) * 4 +
            static_cast<hcpp::u64>(face.num_glyphs - face.num_hmetrics) * 2;
        const hcpp::u64 loca_need =
            static_cast<hcpp::u64>(face.num_glyphs + 1) *
            (face.loca_long ? 4 : 2);
        if (hmtx_need > hmtx_len || loca_need > loca_len) {
            face.data = nullptr;
            return false;
        }
        if (!pick_cmap()) {
            face.data = nullptr;
            return false;
        }
        return true;
    }

    bool ready() noexcept { return face.data != nullptr; }

    hcpp::u16 units_per_em() noexcept { return face.units_per_em; }

    hcpp::i32 ascent() noexcept { return face.asc; }

    hcpp::i32 descent() noexcept { return face.desc; }

    hcpp::i32 line_gap() noexcept { return face.gap; }

    hcpp::u32 glyph_count() noexcept { return face.num_glyphs; }

    hcpp::u16 glyph_index(hcpp::u32 codepoint) noexcept {
        if (face.data == nullptr || codepoint > 0x10FFFF) {
            return 0;
        }
        const hcpp::u16 glyph = face.cmap_fmt == 12
                                    ? cmap_fmt12(codepoint)
                                    : cmap_fmt4(codepoint);
        return glyph < face.num_glyphs ? glyph : 0;
    }

    hcpp::i32 advance(hcpp::u16 glyph) noexcept {
        if (face.data == nullptr || glyph >= face.num_glyphs) {
            return 0;
        }
        const hcpp::u16 index = glyph < face.num_hmetrics
                                    ? glyph
                                    : static_cast<hcpp::u16>(
                                          face.num_hmetrics - 1);
        return ru16(face.data + face.hmtx + index * 4);
    }

    hcpp::i32 left_side_bearing(hcpp::u16 glyph) noexcept {
        if (face.data == nullptr || glyph >= face.num_glyphs) {
            return 0;
        }
        const hcpp::u8* hmtx = face.data + face.hmtx;
        if (glyph < face.num_hmetrics) {
            return ri16(hmtx + glyph * 4 + 2);
        }
        return ri16(hmtx + face.num_hmetrics * 4 +
                    (glyph - face.num_hmetrics) * 2);
    }

    hcpp::i32 kerning(hcpp::u16 left, hcpp::u16 right) noexcept {
        if (face.data == nullptr || face.kern == 0) {
            return 0;
        }
        const hcpp::u8* kern = face.data + face.kern;
        if (ru16(kern) != 0 || ru16(kern + 2) == 0) {
            return 0;
        }
        const hcpp::u8* sub = kern + 4;
        if (sub + 6 > face.data + face.size ||
            (ru16(sub + 4) >> 8) != 0) {
            return 0;
        }
        const hcpp::u16 pairs = ru16(sub + 6);
        if (sub + 14 + static_cast<hcpp::u64>(pairs) * 6 >
            face.data + face.size) {
            return 0;
        }
        const hcpp::u32 key =
            static_cast<hcpp::u32>(left) << 16 | right;
        const hcpp::u8* table = sub + 14;
        hcpp::u32 low = 0;
        hcpp::u32 high = pairs;
        while (low < high) {
            const hcpp::u32 mid = low + (high - low) / 2;
            const hcpp::u32 pair_key = ru32(table + mid * 6);
            if (pair_key < key) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (low >= pairs || ru32(table + low * 6) != key) {
            return 0;
        }
        return ri16(table + low * 6 + 4);
    }

    bool glyph_box(hcpp::u16 glyph, hcpp::i16& x_min, hcpp::i16& y_min,
                   hcpp::i16& x_max, hcpp::i16& y_max) noexcept {
        x_min = y_min = x_max = y_max = 0;
        if (face.data == nullptr || glyph >= face.num_glyphs) {
            return false;
        }
        const hcpp::u32 begin = glyph_offset(glyph);
        const hcpp::u32 end = glyph_offset(glyph + 1);
        if (end <= begin) {
            return true;
        }
        if (end > face.glyf_len || end - begin < 10) {
            return false;
        }
        const hcpp::u8* g = face.data + face.glyf + begin;
        x_min = ri16(g + 2);
        y_min = ri16(g + 4);
        x_max = ri16(g + 6);
        y_max = ri16(g + 8);
        return true;
    }

    const TtfOutline* outline(hcpp::u16 glyph) noexcept {
        if (face.data == nullptr || glyph >= face.num_glyphs) {
            return nullptr;
        }
        TtfOutline* out = &scratch[max_compound_depth];
        *out = empty_outline;
        if (!load_glyph(glyph, 0, out)) {
            return nullptr;
        }
        return out;
    }

}  // namespace hype::font::ttf
