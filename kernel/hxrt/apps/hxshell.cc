// hxshell — a real line-oriented terminal for the HypeAI OS desktop
// (issue #127). Built like the other ported .hxer apps: the Start menu
// opens a wm window named "HxShell" and the registered XAU class adopts
// it, so all paint/key traffic flows through hxshell_wndproc.
//
// Rendering is a monospace grid over the SDF Montserrat stack: every
// cell is drawn at a fixed advance so columns line up like a console.
// The scrollback ring holds committed lines only — the live input row
// is synthesized on top — and repaints honour the wm clip so only the
// rows intersecting the damage rect are re-rasterized.

#include "kernel/hxrt/apps/builtins.h"

#include "hcpp/memory.hpp"
#include "hcpp/string_view.hpp"
#include "kernel/arch/x86/smp.h"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/input/scancode.h"
#include "kernel/log/kring.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/net/dns.h"
#include "kernel/net/net.h"
#include "kernel/net/ntp.h"
#include "kernel/net/ping.h"
#include "kernel/power.h"
#include "kernel/serial.hpp"
#include "kernel/svc/revsvc.h"
#include "kernel/time/rtc.h"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/clipboard.h"
#include "kernel/ui/theme.h"
#include "kernel/vfs/fat32.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/xau.h"
#include "version_generated.h"

namespace hype::hxrt {

    namespace {

        using hype::ui::Rect;
        using hype::win::XAU;
        using hype::win::XauDC;

        constexpr hcpp::u32 k_scrollback = 200;   // committed lines kept
        constexpr hcpp::u32 k_line_max = 120;     // chars per stored line
        constexpr hcpp::u32 k_input_max = 120;    // editable line length
        constexpr hcpp::u32 k_hist_max = 24;      // >= 16 per the spec
        constexpr hcpp::u32 k_px = 14;            // SDF point size
        constexpr hcpp::i32 k_pad_x = 6;
        constexpr hcpp::i32 k_pad_y = 4;
        constexpr hcpp::u32 k_prompt_len = 6;     // "hype> "
        constexpr hcpp::u32 k_blink_timer = 1;
        constexpr hcpp::u64 k_blink_ticks = 50;   // 0.5 s @ 100 Hz PIT
        constexpr hcpp::u32 k_dmesg_tail = 12288;
        constexpr hcpp::u32 k_dmesg_lines = 48;
        constexpr hcpp::u32 k_cat_max = 8192;
        constexpr hcpp::u32 k_ls_max = 48;

        // Terminal palette (always dark, like a real console).
        constexpr hcpp::u32 k_bg = 0x0D1117;
        constexpr hcpp::u32 k_fg = 0xC9D1D9;
        constexpr hcpp::u32 k_dim = 0x8B949E;
        constexpr hcpp::u32 k_err = 0xF85149;
        constexpr hcpp::u32 k_ok = 0x56D364;
        constexpr hcpp::u32 k_prompt_rgb = 0x58A6FF;

        // Line styles stored in the scrollback ring.
        enum : hcpp::u8 {
            st_normal = 0,
            st_dim = 1,
            st_err = 2,
            st_ok = 3,
            st_prompt = 4,
        };

        struct TermLine {
            char text[k_line_max];
            hcpp::u8 len;
            hcpp::u8 style;
        };

        TermLine lines[k_scrollback];
        hcpp::u32 line_head = 0;   // next ring write slot
        hcpp::u32 line_count = 0;  // committed lines in the ring
        hcpp::u32 scroll_back = 0; // rows the view is paged up

        char input_buf[k_input_max];
        hcpp::u32 input_len = 0;
        hcpp::u32 input_cursor = 0;
        hcpp::u32 input_view = 0;  // first rendered column of the row

        char history[k_hist_max][k_input_max];
        hcpp::u8 history_len[k_hist_max];
        hcpp::u32 history_head = 0;
        hcpp::u32 history_count = 0;
        hcpp::i32 history_nav = -1;  // -1 = live draft
        char draft[k_input_max];
        hcpp::u32 draft_len = 0;

        XAU term_win = hype::win::XAU_INVALID;
        hcpp::i32 cell_w = 8;
        hcpp::i32 line_h = 18;
        hcpp::i32 vis_cols = 60;
        hcpp::i32 vis_rows = 16;
        bool cursor_on = true;
        bool focused = true;
        bool booted = false;  // banner emitted (CREATE or first paint)
        hcpp::u32 cmd_count = 0;

        char file_buf[k_cat_max];
        char dmesg_buf[k_dmesg_tail];
        hcpp::u32 dmesg_len = 0;

        void shell_reset() noexcept;

        // ---- tiny string/format helpers -----------------------------------

        hcpp::u32 str_len(const char* text) noexcept {
            hcpp::u32 n = 0;
            while (text != nullptr && text[n] != '\0') {
                ++n;
            }
            return n;
        }

        bool str_equals(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        bool str_prefix(const char* text, const char* prefix) noexcept {
            hcpp::u32 i = 0;
            while (prefix[i] != '\0' && text[i] == prefix[i]) {
                ++i;
            }
            return prefix[i] == '\0';
        }

        void copy_text(char* dst, hcpp::u32 cap, const char* src,
                       hcpp::u32 src_len) noexcept {
            hcpp::u32 i = 0;
            while (i + 1 < cap && i < src_len) {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = '\0';
        }

        struct LBuf {
            char d[128]{};
            hcpp::u32 n = 0;

            void ch(char c) noexcept {
                if (n + 1 < sizeof(d)) {
                    d[n++] = c;
                }
            }
            void s(const char* t) noexcept {
                while (t != nullptr && *t != '\0' && n + 1 < sizeof(d)) {
                    d[n++] = *t++;
                }
            }
            void dec(hcpp::u64 v) noexcept {
                char tmp[20];
                hcpp::u32 m = 0;
                do {
                    tmp[m++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                } while (v != 0 && m < sizeof(tmp));
                while (m != 0) {
                    ch(tmp[--m]);
                }
            }
            void ip(hcpp::u32 v) noexcept {
                for (hcpp::i32 i = 0; i < 4; ++i) {
                    if (i != 0) {
                        ch('.');
                    }
                    dec((v >> (24 - i * 8)) & 0xFFu);
                }
            }
        };

        // ---- scrollback -----------------------------------------------------

        void emit_line(const char* text, hcpp::u32 len,
                       hcpp::u8 style) noexcept {
            if (len > k_line_max) {
                len = k_line_max;
            }
            TermLine& l = lines[line_head];
            for (hcpp::u32 i = 0; i < len; ++i) {
                const char c = text[i];
                // The ring holds no control bytes: cat/dmesg stay clean.
                l.text[i] = (c >= 32 && c <= 126) ? c : '.';
            }
            l.len = static_cast<hcpp::u8>(len);
            l.style = style;
            line_head = (line_head + 1) % k_scrollback;
            if (line_count < k_scrollback) {
                ++line_count;
            }
            scroll_back = 0;
        }

        void emit(const char* text, hcpp::u8 style) noexcept {
            emit_line(text, str_len(text), style);
        }

        void emit(const LBuf& buf, hcpp::u8 style) noexcept {
            emit_line(buf.d, buf.n, style);
        }

        // Splits on '\n' so multi-line payloads (cat, dmesg) land as
        // separate scrollback lines.
        void emit_block(const char* data, hcpp::u32 size,
                        hcpp::u8 style) noexcept {
            hcpp::u32 start = 0;
            for (hcpp::u32 i = 0; i <= size; ++i) {
                if (i == size || data[i] == '\n') {
                    hcpp::u32 end = i;
                    if (end > start && data[end - 1] == '\r') {
                        --end;
                    }
                    emit_line(data + start, end - start, style);
                    start = i + 1;
                }
            }
        }

        TermLine& line_at(hcpp::u32 index) noexcept {
            return lines[(line_head + k_scrollback - line_count + index) %
                         k_scrollback];
        }

        // ---- damage: repaint only the rows that changed ---------------------

        void invalidate_all() noexcept {
            if (term_win != hype::win::XAU_INVALID) {
                hype::win::xau_invalidate(term_win, nullptr);
            }
        }

        hcpp::u32 view_top() noexcept {
            // Logical rows = committed lines + 1 live input row.
            const hcpp::i64 bottom =
                static_cast<hcpp::i64>(line_count) + 1 -
                static_cast<hcpp::i64>(scroll_back);
            const hcpp::i64 top = bottom - vis_rows;
            return static_cast<hcpp::u32>(top < 0 ? 0 : top);
        }

        hcpp::i32 input_row() noexcept {
            return static_cast<hcpp::i32>(line_count) -
                   static_cast<hcpp::i32>(view_top());
        }

        void invalidate_input_row() noexcept {
            const hcpp::i32 row = input_row();
            Rect client{};
            if (term_win == hype::win::XAU_INVALID ||
                !hype::win::xau_client_rect(term_win, client)) {
                return;
            }
            if (row < 0 || row >= vis_rows) {
                return;
            }
            const Rect r{0, k_pad_y + row * line_h, client.w, line_h};
            hype::win::xau_invalidate(term_win, &r);
        }

        void invalidate_cursor() noexcept {
            const hcpp::i32 row = input_row();
            if (term_win == hype::win::XAU_INVALID || row < 0 ||
                row >= vis_rows) {
                return;
            }
            const hcpp::i32 col =
                static_cast<hcpp::i32>(k_prompt_len + input_cursor) -
                static_cast<hcpp::i32>(input_view);
            if (col < 0 || col >= vis_cols) {
                return;
            }
            const Rect r{k_pad_x + col * cell_w, k_pad_y + row * line_h,
                         cell_w, line_h};
            hype::win::xau_invalidate(term_win, &r);
        }

        // ---- grid metrics ----------------------------------------------------

        void metrics() noexcept {
            if (hype::font::hypefont_ready()) {
                hcpp::i32 w = hype::font::text_measure("0", k_px);
                const hcpp::i32 wide = hype::font::text_measure("W", k_px);
                if (wide > w) {
                    w = wide;
                }
                const hcpp::i32 at = hype::font::text_measure("@", k_px);
                if (at > w) {
                    w = at;
                }
                cell_w = w + 2;
                line_h = static_cast<hcpp::i32>(
                    hype::font::text_line_height(k_px));
                if (cell_w < 6) {
                    cell_w = 6;
                }
                if (line_h < 10) {
                    line_h = 10;
                }
                return;
            }
            const hcpp::fonts::FontFace* face =
                hype::ui::theme_active().resolved_font();
            cell_w = face != nullptr && face->advance != 0 ? face->advance
                                                           : 8;
            line_h = face != nullptr ? face->height + 2 : 16;
        }

        void layout(const Rect& client) noexcept {
            metrics();
            vis_cols = (client.w - 2 * k_pad_x) / cell_w;
            if (vis_cols < 4) {
                vis_cols = 4;
            }
            vis_rows = (client.h - 2 * k_pad_y) / line_h;
            if (vis_rows < 1) {
                vis_rows = 1;
            }
            const hcpp::i64 max_back =
                static_cast<hcpp::i64>(line_count) + 1 - vis_rows;
            const hcpp::i64 cap = max_back < 0 ? 0 : max_back;
            if (scroll_back > static_cast<hcpp::u32>(cap)) {
                scroll_back = static_cast<hcpp::u32>(cap);
            }
        }

        // ---- painting ---------------------------------------------------------

        // Draw one glyph cell; y is the top of the row.
        void draw_cell(hype::ui::Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                       char c, hcpp::u32 color, const Rect& clip) noexcept {
            const hcpp::StringView sv{&c, 1};
            if (hype::font::hypefont_ready()) {
                hype::font::text_draw_clip(canvas.target(), x, y, sv, color,
                                           k_px, clip);
                return;
            }
            const hcpp::fonts::FontFace* face =
                hype::ui::theme_active().resolved_font();
            if (face == nullptr) {
                return;
            }
            canvas.draw_text_px(x, y + face->baseline, sv, color, *face);
        }

        hcpp::u32 style_rgb(hcpp::u8 style) noexcept {
            switch (style) {
                case st_dim:
                    return k_dim;
                case st_err:
                    return k_err;
                case st_ok:
                    return k_ok;
                case st_prompt:
                case st_normal:
                default:
                    return k_fg;
            }
        }

        // Draws chars [first,last) of `text`, cell column = index - offset.
        void draw_range(hype::ui::Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                        const char* text, hcpp::u32 first, hcpp::u32 last,
                        hcpp::u32 offset, hcpp::u32 color,
                        const Rect& clip) noexcept {
            for (hcpp::u32 i = first; i < last; ++i) {
                if (i < offset) {
                    continue;
                }
                const hcpp::i32 col = static_cast<hcpp::i32>(i - offset);
                if (col >= vis_cols) {
                    break;
                }
                if (text[i] == ' ') {
                    continue;
                }
                draw_cell(canvas, x + col * cell_w, y, text[i], color,
                          clip);
            }
        }

        void term_paint(hcpp::graphics::Framebuffer& fb, const Rect& client,
                        const Rect& clip) noexcept {
            if (client.empty() || clip.empty()) {
                return;
            }
            if (!booted) {
                shell_reset();
            }
            layout(client);
            hype::ui::Canvas canvas(fb);
            canvas.set_clip(clip);
            canvas.fill_rect(client.intersection(clip),
                             canvas.color(k_bg));

            const hcpp::u32 top = view_top();
            const hcpp::u32 fg = canvas.color(k_fg);
            const hcpp::u32 accent = canvas.color(k_prompt_rgb);
            const hcpp::i32 in_row = input_row();

            for (hcpp::i32 r = 0; r < vis_rows; ++r) {
                const hcpp::i32 y = client.y + k_pad_y + r * line_h;
                const Rect row{client.x, y, client.w, line_h};
                if (!row.intersects(clip)) {
                    continue;
                }
                const hcpp::u32 idx = top + static_cast<hcpp::u32>(r);
                const hcpp::i32 x = client.x + k_pad_x;
                if (idx < line_count) {
                    const TermLine& l = line_at(idx);
                    if (l.style == st_prompt) {
                        // Prompt prefix accent, typed command in fg.
                        const hcpp::u32 split =
                            l.len < k_prompt_len ? l.len : k_prompt_len;
                        draw_range(canvas, x, y, l.text, 0, split, 0,
                                   accent, clip);
                        draw_range(canvas, x, y, l.text, split, l.len, 0,
                                   fg, clip);
                    } else {
                        draw_range(canvas, x, y, l.text, 0, l.len, 0,
                                   canvas.color(style_rgb(l.style)),
                                   clip);
                    }
                } else if (idx == line_count && scroll_back == 0) {
                    // Live input row: "hype> " + the editable buffer.
                    char row_text[k_prompt_len + k_input_max];
                    hcpp::u32 rn = 0;
                    const char* pfx = "hype> ";
                    for (hcpp::u32 i = 0; i < k_prompt_len; ++i) {
                        row_text[rn++] = pfx[i];
                    }
                    for (hcpp::u32 i = 0; i < input_len; ++i) {
                        row_text[rn++] = input_buf[i];
                    }
                    for (hcpp::u32 i = 0; i < rn; ++i) {
                        if (i < input_view || row_text[i] == ' ') {
                            continue;
                        }
                        const hcpp::i32 col =
                            static_cast<hcpp::i32>(i - input_view);
                        if (col >= vis_cols) {
                            break;
                        }
                        draw_cell(canvas, x + col * cell_w, y, row_text[i],
                                  i < k_prompt_len ? accent : fg, clip);
                    }
                    // Block cursor over the cell under edit.
                    if (focused && cursor_on && r == in_row) {
                        const hcpp::i32 ccol =
                            static_cast<hcpp::i32>(k_prompt_len +
                                                   input_cursor) -
                            static_cast<hcpp::i32>(input_view);
                        if (ccol >= 0 && ccol < vis_cols) {
                            const Rect cell{x + ccol * cell_w, y, cell_w,
                                            line_h};
                            canvas.fill_rect_alpha(cell.intersection(row),
                                                   accent, 120);
                        }
                    }
                }
            }
        }

        // ---- input editing -----------------------------------------------------

        void sync_input_view() noexcept {
            const hcpp::u32 col = k_prompt_len + input_cursor;
            if (col < input_view) {
                input_view = col;
            }
            if (col >= input_view + static_cast<hcpp::u32>(vis_cols)) {
                input_view = col - static_cast<hcpp::u32>(vis_cols) + 1;
            }
        }

        void touch_edit() noexcept {
            cursor_on = true;
            // Editing snaps the view back to the live row, like a real
            // terminal does when you type while scrolled up.
            if (scroll_back != 0) {
                scroll_back = 0;
                sync_input_view();
                invalidate_all();
                return;
            }
            sync_input_view();
            invalidate_input_row();
        }

        void history_push() noexcept {
            if (input_len == 0) {
                return;
            }
            // Skip a verbatim repeat of the newest entry.
            if (history_count != 0) {
                const hcpp::u32 newest =
                    (history_head + k_hist_max - 1) % k_hist_max;
                if (history_len[newest] == input_len &&
                    hcpp::compare(history[newest], input_buf,
                                  input_len) == 0) {
                    return;
                }
            }
            char* slot = history[history_head];
            for (hcpp::u32 i = 0; i < input_len; ++i) {
                slot[i] = input_buf[i];
            }
            history_len[history_head] = static_cast<hcpp::u8>(input_len);
            history_head = (history_head + 1) % k_hist_max;
            if (history_count < k_hist_max) {
                ++history_count;
            }
        }

        void history_load(hcpp::i32 nav) noexcept {
            const hcpp::u32 slot =
                (history_head + k_hist_max - history_count +
                 static_cast<hcpp::u32>(nav)) % k_hist_max;
            input_len = history_len[slot];
            for (hcpp::u32 i = 0; i < input_len; ++i) {
                input_buf[i] = history[slot][i];
            }
            input_cursor = input_len;
            touch_edit();
        }

        void history_up() noexcept {
            if (history_count == 0) {
                return;
            }
            if (history_nav < 0) {
                copy_text(draft, sizeof(draft), input_buf, input_len);
                draft_len = input_len;
                history_nav = static_cast<hcpp::i32>(history_count) - 1;
            } else if (history_nav > 0) {
                --history_nav;
            } else {
                return;
            }
            history_load(history_nav);
        }

        void history_down() noexcept {
            if (history_nav < 0) {
                return;
            }
            if (history_nav + 1 < static_cast<hcpp::i32>(history_count)) {
                ++history_nav;
                history_load(history_nav);
                return;
            }
            history_nav = -1;
            input_len = draft_len;
            for (hcpp::u32 i = 0; i < input_len; ++i) {
                input_buf[i] = draft[i];
            }
            input_cursor = input_len;
            touch_edit();
        }

        void input_insert(char c) noexcept {
            if (input_len + 1 >= k_input_max) {
                return;
            }
            for (hcpp::u32 i = input_len; i > input_cursor; --i) {
                input_buf[i] = input_buf[i - 1];
            }
            input_buf[input_cursor++] = c;
            ++input_len;
            touch_edit();
        }

        void input_backspace() noexcept {
            if (input_cursor == 0) {
                return;
            }
            for (hcpp::u32 i = input_cursor - 1; i + 1 < input_len; ++i) {
                input_buf[i] = input_buf[i + 1];
            }
            --input_cursor;
            --input_len;
            touch_edit();
        }

        void input_delete() noexcept {
            if (input_cursor >= input_len) {
                return;
            }
            for (hcpp::u32 i = input_cursor; i + 1 < input_len; ++i) {
                input_buf[i] = input_buf[i + 1];
            }
            --input_len;
            touch_edit();
        }

        void page(hcpp::i32 delta) noexcept {
            hcpp::i64 next = static_cast<hcpp::i64>(scroll_back) + delta;
            const hcpp::i64 max_back =
                static_cast<hcpp::i64>(line_count) + 1 - vis_rows;
            const hcpp::i64 cap = max_back < 0 ? 0 : max_back;
            next = next < 0 ? 0 : (next > cap ? cap : next);
            if (static_cast<hcpp::u32>(next) != scroll_back) {
                scroll_back = static_cast<hcpp::u32>(next);
                invalidate_all();
            }
        }

        void clear_screen() noexcept {
            line_head = 0;
            line_count = 0;
            scroll_back = 0;
            input_len = 0;
            input_cursor = 0;
            input_view = 0;
            history_nav = -1;
            invalidate_all();
        }

        void abort_line() noexcept {
            LBuf b;
            b.s("hype> ");
            for (hcpp::u32 i = 0; i < input_len; ++i) {
                b.ch(input_buf[i]);
            }
            b.s("^C");
            emit(b, st_prompt);
            input_len = 0;
            input_cursor = 0;
            input_view = 0;
            history_nav = -1;
            serial::write_line("hxshell cmd=abort");
            invalidate_all();
        }

        // ---- path helpers ------------------------------------------------------

        // Normalizes loose input into a full "HYPEOS://X/..." path.
        void norm_path(const char* arg, char* out,
                       hcpp::u32 cap) noexcept {
            const char* scheme = "HYPEOS://";
            if (arg != nullptr && str_prefix(arg, scheme)) {
                copy_text(out, cap, arg, str_len(arg));
                return;
            }
            hcpp::u32 o = 0;
            for (hcpp::u32 i = 0; scheme[i] != '\0' && o + 1 < cap; ++i) {
                out[o++] = scheme[i];
            }
            if (arg == nullptr || arg[0] == '\0') {
                out[o++] = 'U';
                if (o + 1 < cap) {
                    out[o++] = '/';
                }
                out[o] = '\0';
                return;
            }
            const char* p = arg;
            char disk = 'U';
            if (arg[1] == ':' || (arg[1] == '\0' && arg[0] != '/')) {
                disk = arg[0];
                p = arg + (arg[1] == ':' ? 2 : 1);
                if (disk >= 'a' && disk <= 'z') {
                    disk = static_cast<char>(disk - 'a' + 'A');
                }
            }
            if (p[0] == '/') {
                ++p;
            }
            if (o + 1 < cap) {
                out[o++] = disk;
            }
            if (o + 1 < cap) {
                out[o++] = '/';
            }
            while (*p != '\0' && o + 1 < cap) {
                out[o++] = *p++;
            }
            out[o] = '\0';
        }

        // ---- commands -----------------------------------------------------------

        bool parse_ip(const char* text, hcpp::u32& out) noexcept {
            hcpp::u32 v = 0;
            hcpp::u32 part = 0;
            hcpp::u32 parts = 0;
            bool any = false;
            for (const char* p = text;; ++p) {
                if (*p >= '0' && *p <= '9') {
                    part = part * 10 + static_cast<hcpp::u32>(*p - '0');
                    if (part > 255) {
                        return false;
                    }
                    any = true;
                } else if (*p == '.' || *p == '\0' || *p == ' ' ||
                           *p == '\t') {
                    if (!any || parts >= 4) {
                        return false;
                    }
                    v = (v << 8) | part;
                    part = 0;
                    ++parts;
                    any = false;
                    if (*p != '.') {
                        break;
                    }
                } else {
                    return false;
                }
            }
            if (parts != 4) {
                return false;
            }
            out = v;
            return true;
        }

        void cmd_help() noexcept {
            emit("hxshell commands:", st_ok);
            emit("  help                  this list", st_dim);
            emit("  echo <text>           print text", st_dim);
            emit("  clear                 wipe screen (Ctrl+L)", st_dim);
            emit("  ver                   OS build info", st_dim);
            emit("  uptime                time since boot", st_dim);
            emit("  mem                   pmm + heap stats", st_dim);
            emit("  cpu                   cpu count + lapic ids", st_dim);
            emit("  ip                    addr, gateway, dns", st_dim);
            emit("  date                  rtc + ntp wall clock", st_dim);
            emit("  ls [path]             list dir (default U:/)",
                 st_dim);
            emit("  cat <path>            print file", st_dim);
            emit("  stat <path>           file metadata", st_dim);
            emit("  mounts                vfs mounts", st_dim);
            emit("  svc list|start|stop <name>", st_dim);
            emit("  dmesg                 tail of kernel log", st_dim);
            emit("  ping <a.b.c.d>        4 icmp echoes", st_dim);
            emit("  resolve <name>        dns A record", st_dim);
            emit("  reboot | shutdown     power control", st_dim);
            emit("  exit                  close terminal", st_dim);
        }

        void cmd_ver() noexcept {
            LBuf b;
            b.s("HypeAI OS " HYPEAI_VERSION_FULL);
            emit(b, st_normal);
            emit("hxrt .hxer app / xau WndProc terminal", st_dim);
        }

        void cmd_uptime() noexcept {
            const hcpp::u64 ms = hype::time::uptime_ms();
            const hcpp::u64 s = ms / 1000;
            LBuf b;
            b.s("up ");
            b.dec(s / 3600);
            b.ch(':');
            const hcpp::u64 m = (s / 60) % 60;
            if (m < 10) {
                b.ch('0');
            }
            b.dec(m);
            b.ch(':');
            const hcpp::u64 sec = s % 60;
            if (sec < 10) {
                b.ch('0');
            }
            b.dec(sec);
            b.s(" (");
            b.dec(ms);
            b.s(" ms)");
            emit(b, st_normal);
        }

        void cmd_mem() noexcept {
            const hype::memory::PhysicalMemoryStats st =
                hype::memory::pmm_stats();
            LBuf b;
            b.s("pmm usable=");
            b.dec(st.usable_bytes / (1024 * 1024));
            b.s(" MiB used=");
            b.dec(st.used_bytes / (1024 * 1024));
            b.s(" MiB free=");
            b.dec(st.free_bytes / (1024 * 1024));
            b.s(" MiB");
            emit(b, st_normal);
            b = LBuf{};
            b.s("heap used=");
            b.dec(hype::memory::heap_used_bytes());
            b.s(" B free=");
            b.dec(hype::memory::heap_free_bytes());
            b.s(" B regions=");
            b.dec(st.usable_regions);
            emit(b, st_normal);
        }

        void cmd_cpu() noexcept {
            const hcpp::size_t n = hype::x86::smp_cpu_count();
            LBuf b;
            b.s("cpus=");
            b.dec(n);
            b.s(" online=");
            b.dec(hype::x86::smp_online_count());
            emit(b, st_normal);
            for (hcpp::size_t i = 0; i < n; ++i) {
                const hype::x86::SmpCpu* c = hype::x86::smp_cpu(i);
                if (c == nullptr) {
                    continue;
                }
                b = LBuf{};
                b.s("cpu");
                b.dec(i);
                b.s(" lapic=");
                b.dec(c->lapic_id);
                b.s(" uid=");
                b.dec(c->acpi_uid);
                b.s(c->is_bsp != 0 ? " bsp" : " ap");
                b.s(c->online != 0 ? " online" : " parked");
                emit(b, st_dim);
            }
        }

        void cmd_ip() noexcept {
            LBuf b;
            b.s("link=");
            b.s(hype::net::net_ready() ? "up" : "down");
            b.s(" ip=");
            b.ip(hype::net::net_ip());
            emit(b, st_normal);
            b = LBuf{};
            b.s("gw=");
            b.ip(hype::net::net_gateway());
            b.s(" dns=");
            b.ip(hype::net::net_dns_ip());
            b.s(" ntp=");
            b.ip(hype::net::net_ntp_ip());
            emit(b, st_normal);
            b = LBuf{};
            b.s("domain=");
            b.s(hype::net::net_dns_domain());
            b.s(" mac=");
            const hcpp::u8* mac = hype::net::net_mac();
            const char* digits = "0123456789abcdef";
            for (hcpp::i32 i = 0; i < 6; ++i) {
                if (i != 0) {
                    b.ch(':');
                }
                const hcpp::u8 v = mac != nullptr ? mac[i] : 0;
                b.ch(digits[v >> 4]);
                b.ch(digits[v & 0xF]);
            }
            emit(b, st_dim);
        }

        void cmd_date() noexcept {
            char buf[20];
            if (hype::time::rtc_format(buf)) {
                LBuf b;
                b.s("rtc ");
                b.s(buf);
                emit(b, st_normal);
            } else {
                emit("rtc unavailable", st_err);
            }
            if (hype::net::ntp_synced()) {
                LBuf b;
                b.s("unix ");
                b.dec(hype::net::net_time_unix());
                emit(b, st_normal);
            } else {
                emit("ntp not synced", st_dim);
            }
        }

        void cmd_ls(const char* arg) noexcept {
            char path[hype::vfs::hypepath_max_length];
            norm_path(arg, path, sizeof(path));
            hype::vfs::FsStat st{};
            if (hype::vfs::fs_stat(path, st) == hype::vfs::FsStatus::Ok &&
                (st.flags & hype::vfs::FS_DIR) == 0) {
                LBuf b;
                b.s(st.name);
                b.s("  ");
                b.dec(st.size);
                b.s(" B");
                emit(b, st_normal);
                return;
            }
            hype::vfs::FsStat items[k_ls_max];
            const hcpp::u32 n = hype::vfs::fs_list(path, items, k_ls_max);
            if (n == 0) {
                LBuf b;
                b.s("ls: empty or missing: ");
                b.s(path);
                emit(b, st_err);
                return;
            }
            for (hcpp::u32 i = 0; i < n; ++i) {
                LBuf b;
                b.s(items[i].name);
                if ((items[i].flags & hype::vfs::FS_DIR) != 0) {
                    b.ch('/');
                    emit(b, st_ok);
                } else {
                    b.s("  ");
                    b.dec(items[i].size);
                    b.s(" B");
                    emit(b, st_normal);
                }
            }
            LBuf b;
            b.s("-- ");
            b.dec(n);
            b.s(n == 1 ? " entry" : " entries");
            emit(b, st_dim);
        }

        void cmd_cat(const char* arg) noexcept {
            if (arg == nullptr || arg[0] == '\0') {
                emit("usage: cat <path>", st_err);
                return;
            }
            char path[hype::vfs::hypepath_max_length];
            norm_path(arg, path, sizeof(path));
            hype::vfs::FsStat st{};
            if (hype::vfs::fs_stat(path, st) != hype::vfs::FsStatus::Ok) {
                LBuf b;
                b.s("cat: not found: ");
                b.s(path);
                emit(b, st_err);
                return;
            }
            if ((st.flags & hype::vfs::FS_DIR) != 0) {
                emit("cat: is a directory", st_err);
                return;
            }
            hcpp::u32 got = 0;
            if (hype::vfs::fs_read(path, file_buf, sizeof(file_buf),
                                   &got) != hype::vfs::FsStatus::Ok) {
                emit("cat: read failed", st_err);
                return;
            }
            emit_block(file_buf, got, st_normal);
            if (st.size > sizeof(file_buf)) {
                LBuf b;
                b.s("-- truncated at ");
                b.dec(sizeof(file_buf));
                b.s(" of ");
                b.dec(st.size);
                b.s(" B --");
                emit(b, st_dim);
            }
        }

        void cmd_stat(const char* arg) noexcept {
            if (arg == nullptr || arg[0] == '\0') {
                emit("usage: stat <path>", st_err);
                return;
            }
            char path[hype::vfs::hypepath_max_length];
            norm_path(arg, path, sizeof(path));
            hype::vfs::FsStat st{};
            if (hype::vfs::fs_stat(path, st) != hype::vfs::FsStatus::Ok) {
                LBuf b;
                b.s("stat: not found: ");
                b.s(path);
                emit(b, st_err);
                return;
            }
            LBuf b;
            b.s("name=");
            b.s(st.name);
            b.s(" disk=");
            b.ch(st.disk);
            b.s(" size=");
            b.dec(st.size);
            emit(b, st_normal);
            b = LBuf{};
            b.s("flags=");
            if ((st.flags & hype::vfs::FS_DIR) != 0) {
                b.s("dir ");
            }
            b.s((st.flags & hype::vfs::FS_SYS) != 0 ? "sys" : "file");
            emit(b, st_dim);
        }

        void cmd_mounts() noexcept {
            const char disks[] = {'S', 'U', 'T'};
            for (hcpp::u32 i = 0; i < 3; ++i) {
                LBuf b;
                b.s("HYPEOS://");
                b.ch(disks[i]);
                b.s("  used=");
                b.dec(hype::vfs::fs_disk_used(disks[i]) / 1024);
                b.s(" KiB free=");
                b.dec(hype::vfs::fs_disk_free(disks[i]) / 1024);
                b.s(" KiB files=");
                b.dec(hype::vfs::fs_disk_file_count(disks[i]));
                emit(b, st_normal);
            }
            LBuf b;
            b.s("HYPEOS://F  fat32 ");
            b.s(hype::vfs::fat32_mounted() ? "mounted" : "absent");
            emit(b, st_dim);
        }

        void cmd_svc(const char* arg) noexcept {
            if (arg == nullptr || arg[0] == '\0' ||
                str_equals(arg, "list")) {
                const hype::svc::RevService* list = hype::svc::svc_list();
                const hcpp::size_t n = hype::svc::svc_count();
                for (hcpp::size_t i = 0; i < n; ++i) {
                    LBuf b;
                    b.s(list[i].name);
                    b.s("  ");
                    b.s(hype::svc::svc_state_name(list[i].state));
                    if (list[i].autostart) {
                        b.s(" autostart");
                    }
                    emit(b, list[i].state == hype::svc::SvcState::Running
                                ? st_ok
                                : st_normal);
                }
                LBuf b;
                b.s("-- ");
                b.dec(n);
                b.s(" services");
                emit(b, st_dim);
                return;
            }
            const bool want_start = str_prefix(arg, "start ");
            const bool want_stop = str_prefix(arg, "stop ");
            if (!want_start && !want_stop) {
                emit("usage: svc list|start <name>|stop <name>", st_err);
                return;
            }
            const char* name = arg + (want_start ? 6 : 5);
            while (*name == ' ') {
                ++name;
            }
            if (*name == '\0') {
                emit("usage: svc start|stop <name>", st_err);
                return;
            }
            char local[hype::svc::svc_name_max];
            copy_text(local, sizeof(local), name, str_len(name));
            const bool ok = want_start ? hype::svc::svc_start(local)
                                       : hype::svc::svc_stop(local);
            LBuf b;
            b.s(local);
            b.s(want_start ? " start " : " stop ");
            b.s(ok ? "ok -> " : "fail -> ");
            b.s(hype::svc::svc_state_name(hype::svc::svc_state(local)));
            emit(b, ok ? st_ok : st_err);
        }

        // kring_copy_to chunk sink: keeps only the newest bytes so the
        // dmesg tail survives a wrapped 64 KiB ring.
        void dmesg_chunk(const void* data, hcpp::u32 size,
                         void* ctx) noexcept {
            (void)ctx;
            const char* p = static_cast<const char*>(data);
            if (size >= sizeof(dmesg_buf)) {
                hcpp::copy(dmesg_buf, p + size - sizeof(dmesg_buf),
                           sizeof(dmesg_buf));
                dmesg_len = sizeof(dmesg_buf);
                return;
            }
            const hcpp::u32 drop =
                dmesg_len + size > sizeof(dmesg_buf)
                    ? dmesg_len + size - sizeof(dmesg_buf)
                    : 0;
            if (drop != 0) {
                hcpp::move(dmesg_buf, dmesg_buf + drop, dmesg_len - drop);
                dmesg_len -= drop;
            }
            hcpp::copy(dmesg_buf + dmesg_len, p, size);
            dmesg_len += size;
        }

        void cmd_dmesg() noexcept {
            dmesg_len = 0;
            ::kring::kring_copy_to(&dmesg_chunk, nullptr);
            if (dmesg_len == 0) {
                emit("kring empty", st_dim);
                return;
            }
            // Walk back k_dmesg_lines line starts from the tail.
            hcpp::u32 start = dmesg_len;
            hcpp::u32 kept = 0;
            while (start != 0 && kept < k_dmesg_lines) {
                if (dmesg_buf[--start] == '\n' && start + 1 < dmesg_len) {
                    ++kept;
                }
            }
            if (start != 0) {
                ++start;
            }
            emit_block(dmesg_buf + start, dmesg_len - start, st_dim);
        }

        void cmd_ping(const char* arg) noexcept {
            hcpp::u32 ip = 0;
            if (arg == nullptr || !parse_ip(arg, ip)) {
                emit("usage: ping <a.b.c.d>", st_err);
                return;
            }
            LBuf b;
            b.s("pinging ");
            b.ip(ip);
            b.s(" (blocking, up to ~4s)...");
            emit(b, st_dim);
            const hcpp::u32 replies = hype::net::ping(ip, 4);
            b = LBuf{};
            b.s("replies ");
            b.dec(replies);
            b.s("/4 (per-reply detail on serial)");
            emit(b, replies != 0 ? st_ok : st_err);
        }

        void cmd_resolve(const char* arg) noexcept {
            if (arg == nullptr || arg[0] == '\0') {
                emit("usage: resolve <name>", st_err);
                return;
            }
            char name[64];
            copy_text(name, sizeof(name), arg, str_len(arg));
            LBuf b;
            b.s("resolving ");
            b.s(name);
            b.s(" (blocking, up to 3s)...");
            emit(b, st_dim);
            hcpp::u32 ip = 0;
            const bool ok = hype::net::dns_resolve(name, &ip, 3000);
            b = LBuf{};
            b.s(name);
            b.s(" = ");
            if (ok) {
                b.ip(ip);
            } else {
                b.s("fail");
            }
            emit(b, ok ? st_ok : st_err);
        }

        // ---- dispatch -----------------------------------------------------------

        void run_command(char* line) noexcept {
            char* p = line;
            while (*p == ' ') {
                ++p;
            }
            if (*p == '\0') {
                return;
            }
            char* args = p;
            while (*args != '\0' && *args != ' ') {
                ++args;
            }
            if (*args == ' ') {
                *args++ = '\0';
                while (*args == ' ') {
                    ++args;
                }
            }
            ++cmd_count;
            serial::write("hxshell cmd=");
            serial::write_dec32(cmd_count);
            serial::write("\r\n");

            if (str_equals(p, "help")) {
                cmd_help();
            } else if (str_equals(p, "echo")) {
                emit(args, st_normal);
            } else if (str_equals(p, "clear")) {
                clear_screen();
            } else if (str_equals(p, "ver")) {
                cmd_ver();
            } else if (str_equals(p, "uptime")) {
                cmd_uptime();
            } else if (str_equals(p, "mem")) {
                cmd_mem();
            } else if (str_equals(p, "cpu")) {
                cmd_cpu();
            } else if (str_equals(p, "ip")) {
                cmd_ip();
            } else if (str_equals(p, "date")) {
                cmd_date();
            } else if (str_equals(p, "ls") || str_equals(p, "dir")) {
                cmd_ls(args);
            } else if (str_equals(p, "cat")) {
                cmd_cat(args);
            } else if (str_equals(p, "stat")) {
                cmd_stat(args);
            } else if (str_equals(p, "mounts")) {
                cmd_mounts();
            } else if (str_equals(p, "svc")) {
                cmd_svc(args);
            } else if (str_equals(p, "dmesg")) {
                cmd_dmesg();
            } else if (str_equals(p, "ping")) {
                cmd_ping(args);
            } else if (str_equals(p, "resolve")) {
                cmd_resolve(args);
            } else if (str_equals(p, "reboot")) {
                emit("rebooting...", st_err);
                hype::power::power_restart();
            } else if (str_equals(p, "shutdown")) {
                emit("shutting down...", st_err);
                hype::power::power_shutdown();
            } else if (str_equals(p, "exit")) {
                emit("bye", st_dim);
                if (term_win != hype::win::XAU_INVALID) {
                    hype::win::xau_destroy(term_win);
                }
            } else {
                LBuf b;
                b.s("unknown command: ");
                b.s(p);
                b.s(" - try help");
                emit(b, st_err);
            }
        }

        void commit_line() noexcept {
            history_push();
            // Commit the "hype> <input>" row into the scrollback.
            LBuf b;
            b.s("hype> ");
            for (hcpp::u32 i = 0; i < input_len; ++i) {
                b.ch(input_buf[i]);
            }
            emit(b, st_prompt);
            char cmd[k_input_max];
            copy_text(cmd, sizeof(cmd), input_buf, input_len);
            input_len = 0;
            input_cursor = 0;
            input_view = 0;
            history_nav = -1;
            scroll_back = 0;
            if (cmd[0] != '\0') {
                run_command(cmd);
            }
            cursor_on = true;
            invalidate_all();
        }

        void shell_reset() noexcept {
            line_head = 0;
            line_count = 0;
            scroll_back = 0;
            input_len = 0;
            input_cursor = 0;
            input_view = 0;
            history_head = 0;
            history_count = 0;
            history_nav = -1;
            draft_len = 0;
            cmd_count = 0;
            cursor_on = true;
            emit("HypeAI OS " HYPEAI_VERSION_FULL, st_dim);
            emit("hxshell ready - type 'help' for commands", st_dim);
            booted = true;
        }

        void on_keydown(hcpp::u32 code, hcpp::u32 ascii) noexcept {
            const bool ctrl = hype::input::scancode_key_pressed(29);
            if (ctrl && code == 38) {
                clear_screen();
                return;
            }
            if (ctrl && code == 46) {
                // Terminal semantics: the live input line is the copy
                // source (pushed to the system clipboard), then ^C
                // aborts it — like copying a selection then breaking.
                // If winman's XAU_MSG_COPY dispatch ran first it pushed
                // the same text; clip_set() collapses the duplicate.
                if (input_len != 0) {
                    hype::ui::clip_set(input_buf, input_len);
                }
                abort_line();
                return;
            }
            switch (code) {
                case 28:  // enter (main + keypad both decode to 28)
                    commit_line();
                    return;
                case 14:  // backspace
                    input_backspace();
                    return;
                case 83:  // delete (E0 53)
                    input_delete();
                    return;
                case 75:  // left
                    if (input_cursor != 0) {
                        --input_cursor;
                        touch_edit();
                    }
                    return;
                case 77:  // right
                    if (input_cursor < input_len) {
                        ++input_cursor;
                        touch_edit();
                    }
                    return;
                case 71:  // home
                    input_cursor = 0;
                    touch_edit();
                    return;
                case 79:  // end
                    input_cursor = input_len;
                    touch_edit();
                    return;
                case 72:  // up
                    history_up();
                    return;
                case 80:  // down
                    history_down();
                    return;
                case 73:  // page up
                    page(vis_rows > 1 ? vis_rows - 1 : 1);
                    return;
                case 81:  // page down
                    page(-(vis_rows > 1 ? vis_rows - 1 : 1));
                    return;
                default:
                    (void)ascii;
                    return;
            }
        }

        // ---- XAU window -----------------------------------------------------------

        hcpp::i32 hxshell_wndproc(XAU xau_win, hcpp::u32 msg,
                                  hcpp::u64 wparam,
                                  hcpp::u64 lparam) noexcept {
            switch (msg) {
                case hype::win::XAU_MSG_CREATE:
                    term_win = xau_win;
                    shell_reset();
                    hype::win::xau_set_title(xau_win, "hxshell");
                    hype::win::xau_set_timer(xau_win, k_blink_timer,
                                             k_blink_ticks);
                    serial::write_line("hxshell open=ok");
                    return 0;
                case hype::win::XAU_MSG_DESTROY:
                    term_win = hype::win::XAU_INVALID;
                    return 0;
                case hype::win::XAU_MSG_PAINT: {
                    const XauDC* dc =
                        reinterpret_cast<const XauDC*>(lparam);
                    if (dc != nullptr && dc->fb != nullptr &&
                        dc->fb->valid() && !dc->client.empty()) {
                        term_paint(*dc->fb, dc->client, dc->clip);
                    }
                    return 0;
                }
                case hype::win::XAU_MSG_SIZE:
                    invalidate_all();
                    return 0;
                case hype::win::XAU_MSG_KEYDOWN:
                    on_keydown(static_cast<hcpp::u32>(wparam & 0xFF),
                               static_cast<hcpp::u32>(lparam & 0xFF));
                    return 0;
                case hype::win::XAU_MSG_CHAR:
                    input_insert(static_cast<char>(wparam & 0x7F));
                    return 0;
                case hype::win::XAU_MSG_COPY:
                    // Clipboard source: the live input line.
                    if (input_len != 0) {
                        hype::ui::clip_set(input_buf, input_len);
                    }
                    return 0;
                case hype::win::XAU_MSG_CUT:
                    if (input_len != 0) {
                        hype::ui::clip_set(input_buf, input_len);
                        input_len = 0;
                        input_cursor = 0;
                        input_view = 0;
                        history_nav = -1;
                        invalidate_all();
                    }
                    return 0;
                case hype::win::XAU_MSG_PASTE: {
                    hcpp::u32 len = 0;
                    const char* text = hype::ui::clip_get(len);
                    if (text != nullptr) {
                        for (hcpp::u32 i = 0; i < len; ++i) {
                            const char c = text[i];
                            if (c >= 32 && c < 127) {
                                input_insert(c);
                            }
                        }
                    }
                    return 0;
                }
                case hype::win::XAU_MSG_FOCUS:
                    focused = wparam != 0;
                    invalidate_cursor();
                    return 0;
                case hype::win::XAU_MSG_TIMER:
                    if (wparam == k_blink_timer) {
                        cursor_on = !cursor_on;
                        invalidate_cursor();
                    }
                    return 0;
                case hype::win::XAU_MSG_LBUTTONDOWN:
                    hype::win::xau_focus(xau_win);
                    return 0;
                default:
                    return hype::win::xau_defproc(xau_win, msg, wparam,
                                                  lparam);
            }
        }

        // .hxer fallback entry: renders the same terminal when the window
        // somehow bypasses XAU adoption (e.g. direct hxrt_exec tests).
        void hxshell_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            term_paint(*ctx.fb, ctx.region, ctx.paint_clip());
        }

        void hxshell_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "HxShell", "HYPEOS://S/apps/HxShell.hxersys",
            hxshell_entry, hxshell_close, nullptr};

    }

    const HxerHeader* hxshell_app() noexcept { return &desc; }

    // Registers the "HxShell" XAU class so wm windows of this app run
    // the terminal WndProc (paint + keyboard through XAU_MSG_*).
    void hxshell_register_xau() noexcept {
        hype::win::xau_register_class("HxShell", &hxshell_wndproc, 0, 0);
    }

}
