#include "kernel/input/cursprite.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/codec.h"
#include "kernel/codecapps/registry.h"
#include "kernel/display/scaler.h"

namespace hype::input {

    namespace {

        const char cursor_mask[cursor_sprite_h][cursor_sprite_w + 1] = {
            "X.............",
            "XX............",
            "XXX...........",
            "XXXX..........",
            "XXXXX.........",
            "XXXXXX........",
            "XXXXXXX.......",
            "XXXXXXXX......",
            "XXXXXXXXX.....",
            "XXXXXXXXXX....",
            "XXXXXXXXXXX...",
            "XXXXXXXXXXXX..",
            "XXXXXXXXXXXXX.",
            "XXXXXXXXXXXXXX",
            "XXXXXXXXX.....",
            "XXXXX..XXX....",
            "XXXX....XXX...",
            "XXX.....XXX...",
            "XX......XXX...",
            "X.......XXX...",
            ".........XXX..",
            "........XXX...",
            "........XXX...",
            "........XX....",
        };

        // Decoded PNG sprite cache: packed u32 pixels in frame byte order
        // (little-endian RGBA bytes -> A<<24 | B<<16 | G<<8 | R).
        constexpr hcpp::size_t cursor_arena_pages = 896;  // ~3.5MB decode arena

        hcpp::u32 png_sprite[cursor_png_sprite_w * cursor_png_sprite_h];
        hcpp::i32 png_hot_x = 0;
        hcpp::i32 png_hot_y = 0;
        bool png_ready = false;

        hcpp::u32 under_px[cursor_png_sprite_w * cursor_png_sprite_h];
        hcpp::i32 under_x = 0;
        hcpp::i32 under_y = 0;
        hcpp::i32 under_w = 0;
        hcpp::i32 under_h = 0;
        bool under_saved = false;

        bool mask_at(hcpp::i32 x, hcpp::i32 y) noexcept {
            if (x < 0 || y < 0 || x >= cursor_sprite_w || y >= cursor_sprite_h) {
                return false;
            }
            return cursor_mask[y][x] == 'X';
        }

        void plot(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y,
                  hcpp::u32 color, hcpp::u32 &drawn) noexcept {
            if (x < 0 || y < 0 || x >= static_cast<hcpp::i32>(fb.width()) ||
                y >= static_cast<hcpp::i32>(fb.height())) {
                return;
            }
            fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y), color);
            ++drawn;
        }

        void plot_blend(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y,
                        hcpp::u32 argb, hcpp::u32 &drawn) noexcept {
            const hcpp::u32 a = argb >> 24;
            if (a == 0 || x < 0 || y < 0 ||
                x >= static_cast<hcpp::i32>(fb.width()) ||
                y >= static_cast<hcpp::i32>(fb.height())) {
                return;
            }
            const hcpp::u8 sr = static_cast<hcpp::u8>(argb & 0xFF);
            const hcpp::u8 sg = static_cast<hcpp::u8>((argb >> 8) & 0xFF);
            const hcpp::u8 sb = static_cast<hcpp::u8>((argb >> 16) & 0xFF);
            if (a == 255) {
                fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                             fb.encode_rgb(sr, sg, sb));
                ++drawn;
                return;
            }
            hcpp::u8 dr = 0;
            hcpp::u8 dg = 0;
            hcpp::u8 db = 0;
            fb.decode_rgb(fb.get_pixel(static_cast<hcpp::u32>(x),
                                       static_cast<hcpp::u32>(y)),
                          dr, dg, db);
            const hcpp::u32 inv = 255 - a;
            const hcpp::u8 out_r = static_cast<hcpp::u8>(
                (static_cast<hcpp::u32>(sr) * a + static_cast<hcpp::u32>(dr) * inv + 127) / 255);
            const hcpp::u8 out_g = static_cast<hcpp::u8>(
                (static_cast<hcpp::u32>(sg) * a + static_cast<hcpp::u32>(dg) * inv + 127) / 255);
            const hcpp::u8 out_b = static_cast<hcpp::u8>(
                (static_cast<hcpp::u32>(sb) * a + static_cast<hcpp::u32>(db) * inv + 127) / 255);
            fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                         fb.encode_rgb(out_r, out_g, out_b));
            ++drawn;
        }

        bool build_png_sprite() noexcept {
            codec::Arena arena{};
            if (!codec::arena_from_pmm(arena, cursor_arena_pages)) {
                return false;
            }
            codec::FrameBuf frame{};
            const codec::CodecStatus status =
                codec::codec_decode(codec::CodecId::Png, cursor_png_data,
                                    cursor_png_data_size, &frame, arena);
            bool ok = status == codec::CodecStatus::Ok && frame.data != nullptr &&
                      frame.format == video::PixelFormat::RGB32 &&
                      frame.width != 0 && frame.height != 0;
            hcpp::u32 min_x = 0;
            hcpp::u32 min_y = 0;
            hcpp::u32 max_x = 0;
            hcpp::u32 max_y = 0;
            bool any = false;
            if (ok) {
                min_x = frame.width;
                min_y = frame.height;
                for (hcpp::u32 y = 0; y < frame.height; ++y) {
                    const hcpp::u8 *row =
                        frame.data + static_cast<hcpp::size_t>(y) * frame.pitch;
                    for (hcpp::u32 x = 0; x < frame.width; ++x) {
                        if (row[static_cast<hcpp::size_t>(x) * 4 + 3] == 0) {
                            continue;
                        }
                        any = true;
                        if (x < min_x) min_x = x;
                        if (x > max_x) max_x = x;
                        if (y < min_y) min_y = y;
                        if (y > max_y) max_y = y;
                    }
                }
            }
            ok = ok && any;
            if (ok) {
                const hcpp::u32 *src = reinterpret_cast<const hcpp::u32 *>(frame.data);
                const display::ScaleRect src_rect{min_x, min_y, max_x - min_x + 1,
                                                  max_y - min_y + 1};
                const display::ScaleRect dst_rect{0, 0, cursor_png_sprite_w,
                                                  cursor_png_sprite_h};
                ok = display::scale_bilinear(
                    src, frame.width, frame.height, frame.pitch / 4, src_rect,
                    png_sprite, cursor_png_sprite_w, cursor_png_sprite_h,
                    cursor_png_sprite_w, dst_rect);
            }
            if (ok) {
                // Hotspot = topmost solid (alpha >= 128) pixel: the arrow tip.
                bool found = false;
                for (hcpp::i32 y = 0; y < cursor_png_sprite_h && !found; ++y) {
                    for (hcpp::i32 x = 0; x < cursor_png_sprite_w; ++x) {
                        if ((png_sprite[y * cursor_png_sprite_w + x] >> 24) >= 128) {
                            png_hot_x = x;
                            png_hot_y = y;
                            found = true;
                            break;
                        }
                    }
                }
                ok = found;
            }
            codec::arena_release_pmm(arena, cursor_arena_pages);
            return ok;
        }

    }

    bool cursprite_init() noexcept {
        png_ready = build_png_sprite();
        return png_ready;
    }

    bool cursprite_png_active() noexcept { return png_ready; }

    hcpp::u32 cursprite_erase(hcpp::graphics::Framebuffer &fb) noexcept {
        if (!fb.valid() || !under_saved) {
            return 0;
        }
        hcpp::u32 restored = 0;
        for (hcpp::i32 row = 0; row < under_h; ++row) {
            for (hcpp::i32 col = 0; col < under_w; ++col) {
                fb.put_pixel(static_cast<hcpp::u32>(under_x + col),
                             static_cast<hcpp::u32>(under_y + row),
                             under_px[static_cast<hcpp::u32>(row * under_w + col)]);
                ++restored;
            }
        }
        under_saved = false;
        return restored;
    }

    hcpp::u32 cursprite_draw(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y) noexcept {
        if (!fb.valid()) {
            return 0;
        }
        cursprite_erase(fb);
        const hcpp::i32 spr_w = png_ready ? cursor_png_sprite_w : cursor_sprite_w;
        const hcpp::i32 spr_h = png_ready ? cursor_png_sprite_h : cursor_sprite_h;
        const hcpp::i32 org_x = png_ready ? x - png_hot_x : x;
        const hcpp::i32 org_y = png_ready ? y - png_hot_y : y;
        const hcpp::i32 fw = static_cast<hcpp::i32>(fb.width());
        const hcpp::i32 fh = static_cast<hcpp::i32>(fb.height());
        const hcpp::i32 cx0 = org_x > 0 ? org_x : 0;
        const hcpp::i32 cy0 = org_y > 0 ? org_y : 0;
        const hcpp::i32 cx1 = org_x + spr_w < fw ? org_x + spr_w : fw;
        const hcpp::i32 cy1 = org_y + spr_h < fh ? org_y + spr_h : fh;
        if (cx0 < cx1 && cy0 < cy1) {
            under_w = cx1 - cx0;
            under_h = cy1 - cy0;
            under_x = cx0;
            under_y = cy0;
            for (hcpp::i32 row = 0; row < under_h; ++row) {
                for (hcpp::i32 col = 0; col < under_w; ++col) {
                    under_px[static_cast<hcpp::u32>(row * under_w + col)] =
                        fb.get_pixel(static_cast<hcpp::u32>(under_x + col),
                                     static_cast<hcpp::u32>(under_y + row));
                }
            }
            under_saved = true;
        }
        if (png_ready) {
            const hcpp::i32 origin_x = x - png_hot_x;
            const hcpp::i32 origin_y = y - png_hot_y;
            hcpp::u32 drawn = 0;
            for (hcpp::i32 row = 0; row < cursor_png_sprite_h; ++row) {
                for (hcpp::i32 col = 0; col < cursor_png_sprite_w; ++col) {
                    plot_blend(fb, origin_x + col, origin_y + row,
                               png_sprite[row * cursor_png_sprite_w + col], drawn);
                }
            }
            return drawn;
        }
        const hcpp::u32 outline = fb.encode_rgb(12, 12, 16);
        const hcpp::u32 fill = fb.encode_rgb(250, 250, 255);
        hcpp::u32 drawn = 0;
        for (hcpp::i32 row = 0; row < cursor_sprite_h; ++row) {
            for (hcpp::i32 col = 0; col < cursor_sprite_w; ++col) {
                const bool edge = mask_at(col, row) &&
                                  (!mask_at(col - 1, row) || !mask_at(col + 1, row) ||
                                   !mask_at(col, row - 1) || !mask_at(col, row + 1));
                if (edge) {
                    plot(fb, x + col, y + row, outline, drawn);
                }
            }
        }
        for (hcpp::i32 row = 0; row < cursor_sprite_h; ++row) {
            for (hcpp::i32 col = 0; col < cursor_sprite_w; ++col) {
                if (mask_at(col, row)) {
                    const bool edge = !mask_at(col - 1, row) || !mask_at(col + 1, row) ||
                                      !mask_at(col, row - 1) || !mask_at(col, row + 1);
                    if (!edge) {
                        plot(fb, x + col, y + row, fill, drawn);
                    }
                }
            }
        }
        return drawn;
    }

    hcpp::u32 cursprite_opaque_count() noexcept {
        hcpp::u32 count = 0;
        for (hcpp::i32 row = 0; row < cursor_sprite_h; ++row) {
            for (hcpp::i32 col = 0; col < cursor_sprite_w; ++col) {
                if (cursor_mask[row][col] == 'X') {
                    ++count;
                }
            }
        }
        return count;
    }

    hcpp::u32 cursprite_test_fill(hcpp::u32 *pixels, hcpp::u32 capacity) noexcept {
        if (pixels == nullptr || capacity < static_cast<hcpp::u32>(cursor_sprite_w * cursor_sprite_h)) {
            return 0;
        }
        hcpp::u32 n = 0;
        for (hcpp::i32 row = 0; row < cursor_sprite_h; ++row) {
            for (hcpp::i32 col = 0; col < cursor_sprite_w; ++col) {
                pixels[n++] = mask_at(col, row) ? 1u : 0u;
            }
        }
        return n;
    }

}
