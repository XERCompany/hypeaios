#include "kernel/console/fbcon.h"

#include "hcpp/fonts/font.hpp"
#include "kernel/functionemitkeys.h"
#include "kernel/input/hypeptr.h"
#include "kernel/input/scancode.h"
#include "kernel/log/kring.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/win/winman.h"

extern "C" void* memmove(void* dest, const void* src, hcpp::size_t n) noexcept;

namespace hype::console {

    namespace {

        // Scrollback geometry: enough columns for a 1920 px scanout at
        // 8 px cells, and a bounded line ring so toggling the console on
        // can repaint the recent kernel log. All static, no heap.
        constexpr hcpp::u32 max_cols = 240;
        constexpr hcpp::u32 sb_lines = 256;

        // Set-1 scancodes for the debug-console chord and scrollback
        // paging. Ctrl+Alt+F1 is chosen because the existing chords
        // (Ctrl+Alt+L lock, Ctrl+Alt+Del/End power, Win combos, Alt-Tab)
        // leave it free.
        constexpr hcpp::u16 key_ctrl = 29;
        constexpr hcpp::u16 key_alt = 56;
        constexpr hcpp::u16 key_f1 = 59;
        constexpr hcpp::u16 key_pageup = 73;
        constexpr hcpp::u16 key_pagedown = 81;

        hcpp::graphics::Framebuffer con_fb;
        const hcpp::fonts::FontFace* con_font = nullptr;
        hcpp::u32 cell_w = 8;
        hcpp::u32 cell_h = 16;
        hcpp::u32 con_cols = 0;
        hcpp::u32 con_rows = 0;
        hcpp::u32 color_fg = 0;
        hcpp::u32 color_bg = 0;

        bool ready_ = false;
        bool shown_ = false;
        bool want_show = false;
        hcpp::i32 req_scroll = 0;
        bool keys_hooked = false;

        // Line ring: completed lines 0..sb_count-1 (ring order starting
        // at sb_head), then the line being composed at logical index
        // sb_count. view_up > 0 freezes the view on older lines.
        char sb[sb_lines][max_cols];
        hcpp::u8 sb_len[sb_lines];
        hcpp::u32 sb_head = 0;
        hcpp::u32 sb_count = 0;
        char cur_line[max_cols];
        hcpp::u32 cur_len = 0;
        hcpp::u32 cur_col = 0;
        hcpp::u32 screen_lines = 0;  // rows occupied on the live view
        hcpp::u32 view_up = 0;

        // Same-region fill without per-pixel bounds checks; callers only
        // pass rects that are fully inside the cell grid.
        void fill_raw(hcpp::u32 x, hcpp::u32 y, hcpp::u32 w, hcpp::u32 h,
                      hcpp::u32 color) noexcept {
            volatile hcpp::u8* base = con_fb.base();
            const hcpp::u32 bpp = con_fb.bytes_per_pixel();
            const hcpp::u32 pitch = con_fb.pitch();
            for (hcpp::u32 r = 0; r < h; ++r) {
                volatile hcpp::u8* row =
                    base + static_cast<hcpp::size_t>(y + r) * pitch +
                    static_cast<hcpp::size_t>(x) * bpp;
                for (hcpp::u32 c = 0; c < w; ++c) {
                    if (bpp == 4) {
                        *reinterpret_cast<volatile hcpp::u32*>(row + c * 4) = color;
                    } else {
                        volatile hcpp::u8* px = row + c * 3;
                        px[0] = static_cast<hcpp::u8>(color);
                        px[1] = static_cast<hcpp::u8>(color >> 8);
                        px[2] = static_cast<hcpp::u8>(color >> 16);
                    }
                }
            }
        }

        void draw_cell(hcpp::u32 col, hcpp::u32 row, char ch) noexcept {
            const hcpp::u32 x = col * cell_w;
            const hcpp::u32 y = row * cell_h;
            fill_raw(x, y, cell_w, cell_h, color_bg);
            const hcpp::u8 uc = static_cast<hcpp::u8>(ch);
            if (uc < 0x20 || uc == 0x7F || con_font == nullptr) {
                return;
            }
            const hcpp::fonts::FontGlyph glyph = con_font->glyph_for(uc);
            if (!glyph.valid()) {
                return;
            }
            // Same baseline/bearing math as hcpp::graphics::draw_text so
            // glyphs sit identically inside their cells.
            const hcpp::i64 ox =
                static_cast<hcpp::i64>(x) + static_cast<hcpp::i64>(glyph.bearing_x);
            const hcpp::i64 oy = static_cast<hcpp::i64>(y) +
                                 static_cast<hcpp::i64>(con_font->baseline) -
                                 static_cast<hcpp::i64>(glyph.bearing_y);
            for (hcpp::u32 r = 0; r < glyph.height; ++r) {
                const hcpp::i64 py = oy + static_cast<hcpp::i64>(r);
                if (py < 0) {
                    continue;
                }
                for (hcpp::u32 c = 0; c < glyph.width; ++c) {
                    const hcpp::i64 px = ox + static_cast<hcpp::i64>(c);
                    if (px < 0 || !glyph.pixel(c, r)) {
                        continue;
                    }
                    con_fb.put_pixel(static_cast<hcpp::u32>(px),
                                     static_cast<hcpp::u32>(py), color_fg);
                }
            }
        }

        // logical indexes 0..sb_count; index sb_count is the line being
        // composed.
        const char* line_at(hcpp::u32 logical, hcpp::u32& len) noexcept {
            if (logical == sb_count) {
                len = cur_len;
                return cur_line;
            }
            const hcpp::u32 idx = (sb_head + logical) % sb_lines;
            len = sb_len[idx];
            return sb[idx];
        }

        void commit_line() noexcept {
            hcpp::u32 slot;
            if (sb_count == sb_lines) {
                slot = sb_head;  // evict oldest
                sb_head = (sb_head + 1) % sb_lines;
            } else {
                slot = (sb_head + sb_count) % sb_lines;
                ++sb_count;
            }
            const hcpp::u32 n = cur_len < con_cols ? cur_len : con_cols;
            for (hcpp::u32 i = 0; i < n; ++i) {
                sb[slot][i] = cur_line[i];
            }
            for (hcpp::u32 i = n; i < con_cols; ++i) {
                sb[slot][i] = ' ';
            }
            sb_len[slot] = static_cast<hcpp::u8>(n);
            cur_len = 0;
            cur_col = 0;
        }

        // Scroll the on-screen text area up one row; the dead pixels
        // below the grid stay background from the last clear.
        void scroll_fb() noexcept {
            hcpp::u8* base = const_cast<hcpp::u8*>(con_fb.base());
            const hcpp::u32 pitch = con_fb.pitch();
            const hcpp::u32 text_h = con_rows * cell_h;
            memmove(base, base + static_cast<hcpp::size_t>(pitch) * cell_h,
                    static_cast<hcpp::size_t>(pitch) * (text_h - cell_h));
            fill_raw(0, text_h - cell_h, con_cols * cell_w, cell_h, color_bg);
        }

        void newline() noexcept {
            commit_line();
            if (!shown_ || view_up != 0) {
                return;
            }
            if (screen_lines < con_rows) {
                ++screen_lines;
            } else {
                scroll_fb();
            }
        }

        void put_char(char ch) noexcept {
            const hcpp::u8 uc = static_cast<hcpp::u8>(ch);
            if (ch == '\r') {
                cur_col = 0;
                return;
            }
            if (ch == '\n') {
                newline();
                return;
            }
            if (uc == 0x08) {  // backspace: retreat, content stays
                if (cur_col != 0) {
                    --cur_col;
                }
                return;
            }
            if (ch == '\t') {
                do {
                    put_char(' ');
                } while (cur_col % 8 != 0);
                return;
            }
            if (uc < 0x20) {
                return;  // drop remaining control bytes
            }
            if (cur_col >= con_cols) {
                newline();  // autowrap
            }
            cur_line[cur_col] = uc == 0x7F ? ' ' : ch;
            if (cur_col + 1 > cur_len) {
                cur_len = cur_col + 1;
            }
            if (shown_ && view_up == 0 && screen_lines != 0) {
                draw_cell(cur_col, screen_lines - 1, ch);
            }
            ++cur_col;
        }

        // Repaint the whole view: the last con_rows lines ending
        // view_up lines above the live tail.
        void paint_all() noexcept {
            fill_raw(0, 0, con_fb.width(), con_fb.height(), color_bg);
            const hcpp::u32 total = sb_count + 1;
            const hcpp::u32 end = total > view_up ? total - view_up : 0;
            const hcpp::u32 start = end > con_rows ? end - con_rows : 0;
            hcpp::u32 row = 0;
            for (hcpp::u32 idx = start; idx < end && row < con_rows;
                 ++idx, ++row) {
                hcpp::u32 len = 0;
                const char* text = line_at(idx, len);
                for (hcpp::u32 c = 0; c < len && c < con_cols; ++c) {
                    draw_cell(c, row, text[c]);
                }
            }
            screen_lines = row;
        }

        void apply_show() noexcept {
            view_up = 0;
            hype::input::cursor_erase(con_fb);
            paint_all();
            shown_ = true;
            want_show = true;
            serial::write_line("fbcon show=on");
        }

        void apply_hide() noexcept {
            shown_ = false;
            want_show = false;
            view_up = 0;
            req_scroll = 0;
            serial::write_line("fbcon show=off");
            // Full-shell invalidate: the next desk_paint repaints the
            // compositor surface and flushes it to scanout cleanly.
            hype::win::wm_invalidate_all();
        }

        void kring_chunk(const void* data, hcpp::u32 size, void*) noexcept {
            const char* bytes = static_cast<const char*>(data);
            for (hcpp::u32 i = 0; i < size; ++i) {
                put_char(bytes[i]);
            }
        }

        void con_key_event(void*, const input::InputEvent& e) noexcept {
            // Pressed only — a Repeated F1 (auto-repeat while the chord
            // is held) must not strobe the console on and off.
            if (e.type != input::InputEventType::Key ||
                e.value !=
                    static_cast<hcpp::u32>(input::KeyState::Pressed)) {
                return;
            }
            const bool ctrl = input::scancode_key_pressed(key_ctrl);
            const bool alt = input::scancode_key_pressed(key_alt);
            if (e.code == key_f1 && ctrl && alt) {
                // Flag only — fbcon_poll() performs the repaint in
                // main-loop context, never inside IRQ dispatch.
                want_show = !shown_;
                return;
            }
            if (!shown_) {
                return;
            }
            const hcpp::i32 page =
                static_cast<hcpp::i32>(con_rows > 1 ? con_rows - 1 : 1);
            if (e.code == key_pageup) {
                req_scroll += page;
            } else if (e.code == key_pagedown) {
                req_scroll -= page;
            }
        }

    }  // namespace

    void fbcon_init(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return;
        }
        con_fb = fb;
        con_font = hcpp::fonts::default_font();
        if (con_font != nullptr && con_font->advance != 0 &&
            con_font->height != 0) {
            cell_w = con_font->advance;
            cell_h = con_font->height;
        }
        if (cell_w == 0) {
            cell_w = 8;
        }
        if (cell_h == 0) {
            cell_h = 16;
        }
        con_cols = fb.width() / cell_w;
        con_rows = fb.height() / cell_h;
        if (con_cols > max_cols) {
            con_cols = max_cols;
        }
        if (con_cols == 0 || con_rows == 0) {
            con_cols = 0;
            con_rows = 0;
            return;
        }
        color_fg = fb.encode_rgb(110, 255, 140);  // green-on-black
        color_bg = fb.encode_rgb(0, 0, 0);
        ready_ = true;
        // Replay the kring tail: every serial byte emitted before this
        // point (boot markers, cpu/rtc probes) lives in the crash ring,
        // so draining it seeds the scrollback without a second tap.
        // Screen drawing stays off until fbcon_show().
        kring::kring_copy_to(&kring_chunk, nullptr);
        serial::write("fbcon init=ok ");
        serial::write_dec32(fb.width());
        serial::write_char('x');
        serial::write_dec32(fb.height());
        serial::write("\r\n");
    }

    bool fbcon_ready() noexcept { return ready_; }
    bool fbcon_active() noexcept { return shown_; }

    void fbcon_show(bool visible) noexcept {
        if (!ready_ || visible == shown_) {
            return;
        }
        if (visible) {
            apply_show();
        } else {
            apply_hide();
        }
    }

    void fbcon_toggle() noexcept { want_show = !shown_; }

    void fbcon_putc(char value) noexcept {
        if (!ready_) {
            return;
        }
        put_char(value);
    }

    void fbcon_write(hcpp::StringView text) noexcept {
        for (hcpp::size_t i = 0; i < text.size(); ++i) {
            fbcon_putc(text[i]);
        }
    }

    void fbcon_poll() noexcept {
        if (!ready_) {
            return;
        }
        // The lock veil wins: never leave the klog view up under
        // lockscr, and treat the chord as inert while locked — the
        // scrollback can hold boot details a locked session should not
        // leak; a fresh Ctrl+Alt+F1 after unlock still opens it.
        if (hype::session::session_locked()) {
            if (shown_) {
                apply_hide();
            }
            want_show = false;
        } else if (want_show != shown_) {
            fbcon_show(want_show);
        }
        if (shown_ && req_scroll != 0) {
            const hcpp::i64 next =
                static_cast<hcpp::i64>(view_up) + req_scroll;
            view_up = next < 0 ? 0
                      : (next > static_cast<hcpp::i64>(sb_count)
                             ? sb_count
                             : static_cast<hcpp::u32>(next));
            paint_all();
            req_scroll = 0;
        }
    }

    bool fbcon_bind_keys() noexcept {
        if (keys_hooked) {
            return true;
        }
        keys_hooked = input::subscribe_keys(&con_key_event, nullptr);
        return keys_hooked;
    }

}  // namespace hype::console
