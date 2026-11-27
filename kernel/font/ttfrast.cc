#include "kernel/font/ttfrast.h"

#include "hcpp/memory.hpp"
#include "kernel/font/ttf.h"

namespace hype::font::rast {

    namespace {

        constexpr hcpp::u32 k_max_size_px = 128;
        constexpr hcpp::u32 k_max_dim = 192;
        constexpr hcpp::u32 k_sub_rows = 4;
        constexpr hcpp::u32 k_max_edges = 1600;
        constexpr hcpp::u32 k_max_crossings = 256;
        constexpr hcpp::u32 k_cache_entries = 768;
        constexpr hcpp::size_t k_arena_size = 256 * 1024;

        struct Edge {
            hcpp::i32 x0;
            hcpp::i32 y0;
            hcpp::i32 x1;
            hcpp::i32 y1;
        };

        struct Entry {
            hcpp::u32 codepoint = 0;
            hcpp::u32 size_px = 0;
            hcpp::u32 used = 0;
            RastGlyph glyph;
        };

        Edge edges[k_max_edges];
        hcpp::u32 edge_count = 0;
        bool overflow = false;
        hcpp::i32 scale_q8 = 0;
        hcpp::i32 cross_x[k_max_crossings];
        hcpp::i8 cross_w[k_max_crossings];
        hcpp::u16 row_cover[k_max_dim];
        hcpp::u8 bitmap_scratch[k_max_dim * k_max_dim];
        hcpp::u8 arena[k_arena_size];
        hcpp::size_t arena_used = 0;
        Entry cache[k_cache_entries];
        hcpp::u32 cache_count = 0;
        hcpp::u32 clock_tick = 1;

        hcpp::i32 q8(hcpp::i32 units) noexcept {
            return static_cast<hcpp::i32>(
                (static_cast<hcpp::i64>(units) * scale_q8));
        }

        void add_edge(hcpp::i32 x0, hcpp::i32 y0, hcpp::i32 x1,
                      hcpp::i32 y1) noexcept {
            if (x0 == x1 && y0 == y1) {
                return;
            }
            if (edge_count >= k_max_edges) {
                overflow = true;
                return;
            }
            Edge& e = edges[edge_count++];
            e.x0 = x0;
            e.y0 = y0;
            e.x1 = x1;
            e.y1 = y1;
        }

        void emit_quad(hcpp::i32 x0, hcpp::i32 y0, hcpp::i32 cx, hcpp::i32 cy,
                       hcpp::i32 x1, hcpp::i32 y1) noexcept {
            const hcpp::i32 d2x = x0 - 2 * cx + x1;
            const hcpp::i32 d2y = y0 - 2 * cy + y1;
            hcpp::i32 deviation = (d2x < 0 ? -d2x : d2x) + (d2y < 0 ? -d2y : d2y);
            hcpp::u32 segments =
                static_cast<hcpp::u32>((deviation >> 8) / 2) + 4;
            if (segments > 40) {
                segments = 40;
            }
            hcpp::i32 px = x0;
            hcpp::i32 py = y0;
            for (hcpp::u32 i = 1; i <= segments; ++i) {
                const hcpp::i64 t = static_cast<hcpp::i64>(i) * 256 / segments;
                const hcpp::i64 mt = 256 - t;
                const hcpp::i32 nx = static_cast<hcpp::i32>(
                    (mt * mt * x0 + 2 * mt * t * cx + t * t * x1 + 32768) >>
                    16);
                const hcpp::i32 ny = static_cast<hcpp::i32>(
                    (mt * mt * y0 + 2 * mt * t * cy + t * t * y1 + 32768) >>
                    16);
                add_edge(px, py, nx, ny);
                px = nx;
                py = ny;
            }
        }

        void emit_contour(const ttf::TtfOutline& o, hcpp::u32 first,
                          hcpp::u32 count) noexcept {
            hcpp::i32 sx = 0;
            hcpp::i32 sy = 0;
            hcpp::i32 cx = 0;
            hcpp::i32 cy = 0;
            hcpp::u32 start = 0;
            const hcpp::u32 last = count - 1;
            const bool first_on = o.on_curve[first] != 0;
            const bool last_on = o.on_curve[first + last] != 0;
            if (first_on) {
                sx = q8(o.x[first]);
                sy = -q8(o.y[first]);
                start = 1;
            } else if (last_on) {
                sx = q8(o.x[first + last]);
                sy = -q8(o.y[first + last]);
                start = 0;
            } else {
                sx = (q8(o.x[first + last]) + q8(o.x[first])) / 2;
                sy = -(q8(o.y[first + last]) + q8(o.y[first])) / 2;
                start = 0;
            }
            cx = sx;
            cy = sy;
            hcpp::u32 consumed = 0;
            hcpp::u32 i = start;
            while (consumed < count && !overflow) {
                const hcpp::u32 j = first + (i % count);
                if (o.on_curve[j]) {
                    const hcpp::i32 nx = q8(o.x[j]);
                    const hcpp::i32 ny = -q8(o.y[j]);
                    add_edge(cx, cy, nx, ny);
                    cx = nx;
                    cy = ny;
                    ++i;
                    ++consumed;
                } else {
                    const hcpp::u32 k = first + ((i + 1) % count);
                    hcpp::i32 tx = 0;
                    hcpp::i32 ty = 0;
                    if (o.on_curve[k]) {
                        tx = q8(o.x[k]);
                        ty = -q8(o.y[k]);
                        ++i;
                        ++consumed;
                    } else {
                        tx = (q8(o.x[j]) + q8(o.x[k])) / 2;
                        ty = -(q8(o.y[j]) + q8(o.y[k])) / 2;
                    }
                    emit_quad(cx, cy, q8(o.x[j]), -q8(o.y[j]), tx, ty);
                    cx = tx;
                    cy = ty;
                    ++i;
                    ++consumed;
                }
            }
            add_edge(cx, cy, sx, sy);
        }

        void sort_crossings(hcpp::u32 count) noexcept {
            for (hcpp::u32 i = 1; i < count; ++i) {
                const hcpp::i32 x = cross_x[i];
                const hcpp::i8 w = cross_w[i];
                hcpp::u32 j = i;
                while (j > 0 && cross_x[j - 1] > x) {
                    cross_x[j] = cross_x[j - 1];
                    cross_w[j] = cross_w[j - 1];
                    --j;
                }
                cross_x[j] = x;
                cross_w[j] = w;
            }
        }

        void cover_span(hcpp::i32 x0_px, hcpp::i32 a, hcpp::i32 b) noexcept {
            hcpp::i32 p = a >> 8;
            while ((p << 8) < b) {
                const hcpp::i32 left = a > (p << 8) ? a : (p << 8);
                const hcpp::i32 right = b < ((p + 1) << 8) ? b : ((p + 1) << 8);
                if (right > left) {
                    const hcpp::u32 column =
                        static_cast<hcpp::u32>(p - x0_px);
                    if (column < k_max_dim) {
                        row_cover[column] = static_cast<hcpp::u16>(
                            row_cover[column] +
                            ((right - left) * 255 + 512) / (256 * k_sub_rows));
                    }
                }
                ++p;
            }
        }

        bool rasterize(const ttf::TtfOutline& o, hcpp::u8* bitmap,
                       hcpp::i32& out_w, hcpp::i32& out_h, hcpp::i32& out_bx,
                       hcpp::i32& out_by) noexcept {
            edge_count = 0;
            overflow = false;
            hcpp::u32 first = 0;
            for (hcpp::u32 c = 0; c < o.contour_count; ++c) {
                const hcpp::u32 last = o.contour_end[c];
                if (last < first || last >= o.point_count) {
                    return false;
                }
                emit_contour(o, first, last - first + 1);
                first = last + 1;
            }
            if (overflow || edge_count == 0) {
                out_w = 0;
                out_h = 0;
                out_bx = 0;
                out_by = 0;
                return edge_count == 0 && !overflow;
            }
            hcpp::i32 min_x = edges[0].x0;
            hcpp::i32 max_x = edges[0].x0;
            hcpp::i32 min_y = edges[0].y0;
            hcpp::i32 max_y = edges[0].y0;
            for (hcpp::u32 i = 1; i < edge_count; ++i) {
                const Edge& e = edges[i];
                min_x = e.x0 < min_x ? e.x0 : min_x;
                max_x = e.x0 > max_x ? e.x0 : max_x;
                min_y = e.y0 < min_y ? e.y0 : min_y;
                max_y = e.y0 > max_y ? e.y0 : max_y;
                min_x = e.x1 < min_x ? e.x1 : min_x;
                max_x = e.x1 > max_x ? e.x1 : max_x;
                min_y = e.y1 < min_y ? e.y1 : min_y;
                max_y = e.y1 > max_y ? e.y1 : max_y;
            }
            const hcpp::i32 x0_px = min_x >> 8;
            const hcpp::i32 x1_px = (max_x + 255) >> 8;
            const hcpp::i32 y0_px = min_y >> 8;
            const hcpp::i32 y1_px = (max_y + 255) >> 8;
            out_w = x1_px - x0_px;
            out_h = y1_px - y0_px;
            if (out_w <= 0 || out_h <= 0 ||
                out_w > static_cast<hcpp::i32>(k_max_dim) ||
                out_h > static_cast<hcpp::i32>(k_max_dim) ||
                static_cast<hcpp::u32>(out_w * out_h) > k_arena_size) {
                return false;
            }
            out_bx = x0_px;
            out_by = -y0_px;
            for (hcpp::i32 py = 0; py < out_h; ++py) {
                hcpp::set(row_cover, 0,
                          static_cast<hcpp::size_t>(out_w) * sizeof(hcpp::u16));
                for (hcpp::u32 s = 0; s < k_sub_rows; ++s) {
                    const hcpp::i32 ys =
                        ((y0_px + py) << 8) +
                        static_cast<hcpp::i32>((2 * s + 1) *
                                               (256 / (2 * k_sub_rows)));
                    hcpp::u32 crossings = 0;
                    for (hcpp::u32 i = 0; i < edge_count; ++i) {
                        const Edge& e = edges[i];
                        if (e.y0 == e.y1) {
                            continue;
                        }
                        hcpp::i32 dir = 0;
                        if (e.y0 <= ys && ys < e.y1) {
                            dir = 1;
                        } else if (e.y1 <= ys && ys < e.y0) {
                            dir = -1;
                        } else {
                            continue;
                        }
                        const hcpp::i64 t =
                            (static_cast<hcpp::i64>(ys - e.y0) << 8) /
                            (e.y1 - e.y0);
                        if (crossings >= k_max_crossings) {
                            return false;
                        }
                        cross_x[crossings] = static_cast<hcpp::i32>(
                            e.x0 + ((e.x1 - e.x0) * t >> 8));
                        cross_w[crossings] = static_cast<hcpp::i8>(dir);
                        ++crossings;
                    }
                    if (crossings < 2) {
                        continue;
                    }
                    sort_crossings(crossings);
                    hcpp::i32 winding = 0;
                    hcpp::i32 prev = cross_x[0];
                    for (hcpp::u32 i = 0; i < crossings; ++i) {
                        if (winding != 0 && cross_x[i] > prev) {
                            cover_span(x0_px, prev, cross_x[i]);
                        }
                        winding += cross_w[i];
                        prev = cross_x[i];
                    }
                }
                hcpp::u8* row = bitmap + py * out_w;
                for (hcpp::i32 col = 0; col < out_w; ++col) {
                    row[col] = row_cover[col] > 255
                                   ? 255
                                   : static_cast<hcpp::u8>(row_cover[col]);
                }
            }
            return true;
        }

        Entry* find(hcpp::u32 codepoint, hcpp::u32 size_px) noexcept {
            for (hcpp::u32 i = 0; i < cache_count; ++i) {
                if (cache[i].codepoint == codepoint &&
                    cache[i].size_px == size_px) {
                    return &cache[i];
                }
            }
            return nullptr;
        }

        const RastGlyph* bake(hcpp::u32 codepoint, hcpp::u32 size_px,
                              hcpp::u16 glyph_index) noexcept {
            scale_q8 = static_cast<hcpp::i32>((size_px << 8) /
                                              ttf::units_per_em());
            if (scale_q8 == 0) {
                return nullptr;
            }
            RastGlyph result;
            result.advance_q8 =
                static_cast<hcpp::i32>(ttf::advance(glyph_index)) * scale_q8;
            const ttf::TtfOutline* o = ttf::outline(glyph_index);
            if (o == nullptr) {
                return nullptr;
            }
            hcpp::u8* bitmap = nullptr;
            if (o->contour_count != 0 && o->point_count != 0) {
                hcpp::i32 w = 0;
                hcpp::i32 h = 0;
                hcpp::i32 bx = 0;
                hcpp::i32 by = 0;
                if (!rasterize(*o, bitmap_scratch, w, h, bx, by)) {
                    return nullptr;
                }
                if (w > 0 && h > 0) {
                    const hcpp::size_t need =
                        static_cast<hcpp::size_t>(w) * h;
                    if (need > k_arena_size - arena_used) {
                        cache_count = 0;
                        arena_used = 0;
                    }
                    if (need > k_arena_size - arena_used) {
                        return nullptr;
                    }
                    bitmap = arena + arena_used;
                    arena_used += need;
                    hcpp::copy(bitmap, bitmap_scratch, need);
                    result.width = w;
                    result.height = h;
                    result.bearing_x = bx;
                    result.bearing_y = by;
                    result.alpha = bitmap;
                }
            }
            if (cache_count >= k_cache_entries) {
                hcpp::u32 oldest = 0;
                for (hcpp::u32 i = 1; i < cache_count; ++i) {
                    if (cache[i].used < cache[oldest].used) {
                        oldest = i;
                    }
                }
                cache[oldest] = Entry{};
                cache[oldest].codepoint = codepoint;
                cache[oldest].size_px = size_px;
                cache[oldest].used = clock_tick++;
                cache[oldest].glyph = result;
                return &cache[oldest].glyph;
            }
            Entry& entry = cache[cache_count++];
            entry.codepoint = codepoint;
            entry.size_px = size_px;
            entry.used = clock_tick++;
            entry.glyph = result;
            return &entry.glyph;
        }

    }  // namespace

    const RastGlyph* glyph(hcpp::u32 codepoint, hcpp::u32 size_px) noexcept {
        if (!ttf::ready() || codepoint == 0 || codepoint > 0x10FFFF ||
            size_px == 0 || size_px > k_max_size_px) {
            return nullptr;
        }
        Entry* hit = find(codepoint, size_px);
        if (hit != nullptr) {
            hit->used = clock_tick++;
            return &hit->glyph;
        }
        const hcpp::u16 index = ttf::glyph_index(codepoint);
        if (index == 0) {
            return nullptr;
        }
        return bake(codepoint, size_px, index);
    }

    hcpp::i32 advance_q8(hcpp::u32 codepoint, hcpp::u32 size_px) noexcept {
        if (!ttf::ready() || size_px == 0) {
            return 0;
        }
        const hcpp::u16 index = ttf::glyph_index(codepoint);
        if (index == 0) {
            return 0;
        }
        const hcpp::i32 scale =
            static_cast<hcpp::i32>((size_px << 8) / ttf::units_per_em());
        return static_cast<hcpp::i32>(ttf::advance(index)) * scale;
    }

    void reset() noexcept {
        cache_count = 0;
        arena_used = 0;
    }

}  // namespace hype::font::rast
