#include "kernel/boot/bootsplash.h"

#include "hcpp/fonts/font.hpp"
#include "hcpp/graphics/text.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"

namespace hype::boot {

    namespace {

        constexpr hcpp::u32 wordmark_scale = 4;
        constexpr hcpp::u32 bar_height = 8;
        constexpr hcpp::u32 bar_max_width = 420;
        constexpr hcpp::u32 spinner_dots = 8;
        constexpr hcpp::u32 label_gap = 12;
        constexpr hcpp::i32 spinner_dx[spinner_dots] = {10, 7, 0, -7, -10, -7, 0, 7};
        constexpr hcpp::i32 spinner_dy[spinner_dots] = {0, 7, 10, 7, 0, -7, -10, -7};

        struct SplashState {
            hcpp::graphics::Framebuffer* fb = nullptr;
            hcpp::u32 bg_rgb = 0;
            hcpp::u32 accent_rgb = 0;
            hcpp::u32 fg = 0;
            hcpp::u32 accent = 0;
            hcpp::u32 muted = 0;
            hcpp::u32 muted_fg = 0;
            hcpp::u32 bar_x = 0;
            hcpp::u32 bar_y = 0;
            hcpp::u32 bar_w = 0;
            hcpp::u32 pct = 0;
            hcpp::u32 stage = 0;
            hcpp::u32 spin_fallback = 0;
            hcpp::i32 spin_cx = 0;
            hcpp::i32 spin_cy = 0;
        };

        SplashState state{};

        hcpp::u32 encode(hcpp::graphics::Framebuffer& fb, hcpp::u32 rgb) noexcept {
            return fb.encode_rgb(static_cast<hcpp::u8>(rgb >> 16),
                                 static_cast<hcpp::u8>(rgb >> 8),
                                 static_cast<hcpp::u8>(rgb));
        }

        hcpp::u32 encode_dim(hcpp::graphics::Framebuffer& fb, hcpp::u32 rgb, hcpp::u32 num,
                             hcpp::u32 den) noexcept {
            const hcpp::u32 r = ((rgb >> 16) & 0xFF) * num / den;
            const hcpp::u32 g = ((rgb >> 8) & 0xFF) * num / den;
            const hcpp::u32 b = (rgb & 0xFF) * num / den;
            return fb.encode_rgb(static_cast<hcpp::u8>(r), static_cast<hcpp::u8>(g),
                                 static_cast<hcpp::u8>(b));
        }

        hcpp::u32 bg_row_color(hcpp::u32 y) noexcept {
            const hcpp::u32 h = state.fb->height();
            const hcpp::u32 num = 60 + (h != 0 ? 40 * y / h : 0);
            return encode_dim(*state.fb, state.bg_rgb, num, 100);
        }

        void fill_bg_region(hcpp::u32 x, hcpp::u32 y, hcpp::u32 w, hcpp::u32 h) noexcept {
            for (hcpp::u32 row = 0; row < h; ++row) {
                state.fb->fill_rect(x, y + row, w, 1, bg_row_color(y + row));
            }
        }

        void paint_bar() noexcept {
            state.fb->fill_rect(state.bar_x, state.bar_y, state.bar_w, bar_height, state.muted);
            const hcpp::u32 fill = state.bar_w * state.pct / 100;
            if (fill != 0) {
                state.fb->fill_rect(state.bar_x, state.bar_y, fill, bar_height, state.accent);
            }
        }

        void paint_label(hcpp::StringView name) noexcept {
            const hcpp::u32 label_y = state.bar_y + bar_height + label_gap;
            fill_bg_region(state.bar_x, label_y, state.bar_w, 18);
            boot_draw_text(*state.fb, state.bar_x, label_y, name, state.muted_fg, 1);
        }

    }  // namespace

    void boot_draw_text(hcpp::graphics::Framebuffer& fb, hcpp::u32 x, hcpp::u32 y,
                        hcpp::StringView text, hcpp::u32 color, hcpp::u32 scale) noexcept {
        hcpp::graphics::draw_text(fb, x, y, text, color, scale);
    }

    void boot_splash_begin(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            state.fb = nullptr;
            return;
        }
        const ui::Theme& theme = ui::theme_dark();
        state.fb = &fb;
        state.bg_rgb = theme.bg;
        state.accent_rgb = theme.accent;
        state.fg = encode(fb, theme.fg);
        state.accent = encode(fb, theme.accent);
        state.muted = encode(fb, theme.muted);
        state.muted_fg = encode(fb, theme.muted_fg);
        state.pct = 0;
        state.stage = 0;
        state.spin_fallback = 0;

        for (hcpp::u32 y = 0; y < fb.height(); ++y) {
            fb.fill_rect(0, y, fb.width(), 1, bg_row_color(y));
        }

        const hcpp::fonts::FontFace* font = hcpp::fonts::default_font();
        hcpp::u32 wordmark_w = 0;
        if (font != nullptr) {
            wordmark_w = static_cast<hcpp::u32>(ui::text_extent("HYPEAI OS", *font).width) *
                         wordmark_scale;
        }
        const hcpp::u32 wordmark_h = (font != nullptr ? font->height : 16) * wordmark_scale;
        const hcpp::u32 wordmark_x =
            fb.width() > wordmark_w ? (fb.width() - wordmark_w) / 2 : 0;
        const hcpp::u32 wordmark_y =
            fb.height() > wordmark_h ? (fb.height() * 2 / 5) - wordmark_h / 2 : 0;
        boot_draw_text(fb, wordmark_x, wordmark_y, "HYPEAI OS", state.fg, wordmark_scale);

        state.bar_w = fb.width() - 80 < bar_max_width ? fb.width() - 80 : bar_max_width;
        state.bar_x = (fb.width() - state.bar_w) / 2;
        state.bar_y = fb.height() * 2 / 3;
        state.spin_cx = static_cast<hcpp::i32>(state.bar_x + state.bar_w + 24);
        state.spin_cy = static_cast<hcpp::i32>(state.bar_y + bar_height / 2);

        paint_bar();
        boot_splash_spinner();
        serial::write_line("boot splash=shown");
    }

    void boot_splash_hold() noexcept {
        if (state.fb == nullptr) {
            return;
        }
        if (interrupts::interrupts_initialized()) {
            interrupts::pit_wait_ticks(8);
            return;
        }
        for (volatile hcpp::u32 spin = 0; spin < 40000000; ++spin) {
        }
    }

    void boot_splash_stage(hcpp::StringView name, hcpp::u32 pct) noexcept {
        if (state.fb == nullptr) {
            return;
        }
        state.pct = pct > 100 ? 100 : pct;
        ++state.stage;
        paint_bar();
        paint_label(name);
        boot_splash_spinner();
        serial::write("boot splash stage=");
        serial::write_dec32(state.stage);
        serial::write("\r\n");
    }

    void boot_splash_spinner() noexcept {
        if (state.fb == nullptr) {
            return;
        }
        fill_bg_region(static_cast<hcpp::u32>(state.spin_cx - 14),
                       static_cast<hcpp::u32>(state.spin_cy - 14), 28, 28);
        const hcpp::u64 ticks = interrupts::pit_ticks();
        const hcpp::u32 frame =
            ticks != 0 ? static_cast<hcpp::u32>((ticks / 4) % spinner_dots)
                       : (state.spin_fallback++ % spinner_dots);
        for (hcpp::u32 i = 0; i < spinner_dots; ++i) {
            const hcpp::u32 trail = (i + spinner_dots - frame) % spinner_dots;
            const hcpp::u32 color =
                trail == 0 ? state.accent
                           : trail < 4 ? encode_dim(*state.fb, state.accent_rgb, 8 - trail * 2, 8)
                                       : state.muted;
            state.fb->fill_rect(static_cast<hcpp::u32>(state.spin_cx + spinner_dx[i] - 2),
                                static_cast<hcpp::u32>(state.spin_cy + spinner_dy[i] - 2), 4, 4,
                                color);
        }
    }

    void boot_splash_end() noexcept {
        if (state.fb == nullptr) {
            return;
        }
        for (hcpp::u32 pass = 0; pass < 2; ++pass) {
            for (hcpp::u32 y = 0; y < state.fb->height(); y += 2) {
                for (hcpp::u32 x = 0; x < state.fb->width(); x += 2) {
                    hcpp::u8 r = 0;
                    hcpp::u8 g = 0;
                    hcpp::u8 b = 0;
                    state.fb->decode_rgb(state.fb->get_pixel(x, y), r, g, b);
                    state.fb->fill_rect(x, y, 2, 2,
                                        state.fb->encode_rgb(static_cast<hcpp::u8>(r / 2),
                                                             static_cast<hcpp::u8>(g / 2),
                                                             static_cast<hcpp::u8>(b / 2)));
                }
            }
            interrupts::pit_wait_ticks(2);
        }
        for (hcpp::u32 y = 0; y < state.fb->height(); ++y) {
            state.fb->fill_rect(0, y, state.fb->width(), 1, bg_row_color(y));
        }
        state.fb = nullptr;
        serial::write_line("boot splash=done");
    }

}
