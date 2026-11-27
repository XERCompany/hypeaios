#include "kernel/input/cursprite.h"

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

    }

    hcpp::u32 cursprite_draw(hcpp::graphics::Framebuffer &fb, hcpp::i32 x, hcpp::i32 y) noexcept {
        if (!fb.valid()) {
            return 0;
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
