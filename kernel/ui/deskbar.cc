#include "kernel/ui/deskbar.h"

#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/time/rtc.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/win/deskman.h"
#include "kernel/win/winman.h"

namespace hype::ui {

    namespace {

        using hype::win::Win;
        using hype::win::WinState;

        constexpr hcpp::i32 bar_pad = 4;
        constexpr hcpp::i32 start_w = 84;
        constexpr hcpp::i32 task_w = 148;
        constexpr hcpp::i32 task_gap = 4;
        constexpr hcpp::i32 tray_w = 116;
        constexpr hcpp::i32 clock_w = 44;
        constexpr hcpp::i32 menu_w = 180;
        constexpr hcpp::i32 menu_pad = 4;
        constexpr hcpp::i32 menu_item_h = 24;
        constexpr hcpp::i32 menu_head_h = 24;
        constexpr hcpp::u32 menu_max_items = 12;
        constexpr hcpp::u32 rtc_poll_mask = 31;

        Rect bar{};
        Rect start_btn{};
        hcpp::u32 menu_order[hype::hxrt::hxrt_max_apps];
        hcpp::u32 menu_count = 0;
        bool start_open = false;
        bool painted_once = false;
        bool clock_announced = false;
        hcpp::u8 clock_hour = 0xFF;
        hcpp::u8 clock_minute = 0xFF;
        hcpp::u32 idle_ticks = 0;
        hcpp::u32 win_snap[hype::win::wm_max_windows];
        hcpp::u32 win_snap_count = 0;

        void invalidate(const Rect& region) noexcept {
            if (!region.empty()) {
                hype::win::wm_invalidate(region);
            }
        }

        Rect tray_rect() noexcept {
            return {bar.right() - bar_pad - tray_w, bar.y + bar_pad, tray_w,
                    bar.h - bar_pad * 2};
        }

        Rect clock_rect() noexcept {
            const Rect tray = tray_rect();
            return {tray.right() - clock_w, tray.y, clock_w, tray.h};
        }

        Rect task_btn_rect(hcpp::u32 index) noexcept {
            const hcpp::i32 x =
                start_btn.right() + task_gap +
                static_cast<hcpp::i32>(index) * (task_w + task_gap);
            return {x, bar.y + bar_pad, task_w, bar.h - bar_pad * 2};
        }

        char upper(char c) noexcept {
            return c >= 'a' && c <= 'z'
                       ? static_cast<char>(c - 'a' + 'A')
                       : c;
        }

        bool name_less(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0') {
                const char ca = upper(a[i]);
                const char cb = upper(b[i]);
                if (ca != cb) {
                    return ca < cb;
                }
                ++i;
            }
            return a[i] == '\0' && b[i] != '\0';
        }

        void build_menu_order() noexcept {
            const hcpp::u32 n = hype::hxrt::hxrt_count();
            menu_count = 0;
            for (hcpp::u32 i = 0; i < n && menu_count < menu_max_items; ++i) {
                menu_order[menu_count++] = i;
            }
            const hype::hxrt::HxerHeader* const* list = hype::hxrt::hxrt_list();
            for (hcpp::u32 i = 1; i < menu_count; ++i) {
                const hcpp::u32 key = menu_order[i];
                hcpp::u32 j = i;
                while (j > 0 && name_less(list[key]->name,
                                          list[menu_order[j - 1]]->name)) {
                    menu_order[j] = menu_order[j - 1];
                    --j;
                }
                menu_order[j] = key;
            }
        }

        Rect compute_menu() noexcept {
            if (!start_open) {
                return {};
            }
            const hcpp::u32 rows = menu_count > 0 ? menu_count : 1;
            const hcpp::i32 h = menu_pad * 2 + menu_head_h +
                                static_cast<hcpp::i32>(rows) * menu_item_h;
            Rect r{bar.x + bar_pad, bar.y - 4 - h, menu_w, h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        hcpp::i32 menu_item_at(Point p) noexcept {
            const Rect r = compute_menu();
            if (r.empty() || !r.contains(p)) {
                return -1;
            }
            const hcpp::i32 idx =
                (p.y - r.y - menu_pad - menu_head_h) / menu_item_h;
            if (idx < 0 || idx >= static_cast<hcpp::i32>(menu_count)) {
                return -1;
            }
            return idx;
        }

        void open_start() noexcept {
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            build_menu_order();
            start_open = true;
            serial::write_line("start menu=open");
            invalidate(compute_menu().united(bar));
        }

        void paint_label(Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                         hcpp::StringView text, hcpp::u32 rgb,
                         hcpp::u32 px) noexcept {
            if (hype::font::hypefont_ready()) {
                draw_text_sdf_styled(canvas, x, y, text, canvas.color(rgb),
                                     px, 96, false);
                return;
            }
            const hcpp::fonts::FontFace* face =
                theme_active().resolved_font();
            if (face == nullptr) {
                return;
            }
            canvas.draw_text(x, y + face->baseline, text, canvas.color(rgb),
                             *face);
        }

        hcpp::i32 label_top(const Rect& row) noexcept {
            const hcpp::i32 line = hype::font::hypefont_ready()
                                       ? hype::font::text_line_height(13)
                                       : 16;
            hcpp::i32 top = row.y + (row.h - line) / 2;
            if (top < row.y) {
                top = row.y;
            }
            return top;
        }

        void paint_start_button(Canvas& canvas, const Theme& theme) noexcept {
            rui_control_bg(canvas, start_btn, theme.secondary,
                           start_open ? theme.accent : theme.border);
            // 4-pane window mark.
            const hcpp::i32 gx = start_btn.x + 10;
            const hcpp::i32 gy = start_btn.y + (start_btn.h - 12) / 2;
            const hcpp::u32 glyph = canvas.color(theme.accent);
            canvas.fill_rect(Rect{gx, gy, 5, 5}, glyph);
            canvas.fill_rect(Rect{gx + 7, gy, 5, 5}, glyph);
            canvas.fill_rect(Rect{gx, gy + 7, 5, 5}, glyph);
            canvas.fill_rect(Rect{gx + 7, gy + 7, 5, 5}, glyph);
            paint_label(canvas, gx + 17, label_top(start_btn), "Start",
                        theme.card_fg, 13);
        }

        void paint_task_button(Canvas& canvas, const Theme& theme,
                               const Rect& btn, const Win& w) noexcept {
            const bool focused = w.state == WinState::Focused;
            const bool minimized = w.state == WinState::Minimized;
            rui_control_bg(
                canvas, btn,
                focused ? rui_lift(theme.secondary, 1) : theme.secondary,
                focused ? theme.accent : theme.border);
            paint_label(canvas, btn.x + 10, label_top(btn),
                        hcpp::StringView{w.app_name},
                        minimized ? theme.muted_fg : theme.card_fg, 13);
            if (focused) {
                canvas.fill_rect(
                    Rect{btn.x + 8, btn.bottom() - 3, btn.w - 16, 2},
                    canvas.color(theme.accent));
            } else if (minimized) {
                canvas.fill_rect(
                    Rect{btn.x + 8, btn.bottom() - 3, btn.w - 16, 2},
                    canvas.color(theme.muted_fg));
            }
        }

        void paint_tray(Canvas& canvas, const Theme& theme) noexcept {
            const Rect tray = tray_rect();
            const hcpp::i32 cy = tray.y + tray.h / 2;
            const hcpp::u32 glyph = canvas.color(theme.muted_fg);
            // Speaker placeholder glyph.
            const hcpp::i32 sx = tray.x + 6;
            canvas.fill_rect(Rect{sx, cy - 3, 4, 6}, glyph);
            canvas.draw_line(sx + 4, cy - 2, sx + 9, cy - 6, glyph);
            canvas.draw_line(sx + 4, cy + 2, sx + 9, cy + 6, glyph);
            canvas.draw_line(sx + 9, cy - 6, sx + 9, cy + 6, glyph);
            canvas.draw_line(sx + 11, cy - 2, sx + 12, cy, glyph);
            canvas.draw_line(sx + 12, cy, sx + 11, cy + 2, glyph);
            // Signal/net bars placeholder glyph.
            const hcpp::i32 nx = sx + 18;
            for (hcpp::i32 k = 0; k < 4; ++k) {
                canvas.fill_rect(
                    Rect{nx + k * 4, cy + 5 - (k + 1) * 2, 3, (k + 1) * 2},
                    glyph);
            }
            // Clock "HH:MM".
            char clk[6]{"--:--"};
            if (clock_hour != 0xFF) {
                clk[0] = static_cast<char>('0' + clock_hour / 10);
                clk[1] = static_cast<char>('0' + clock_hour % 10);
                clk[2] = ':';
                clk[3] = static_cast<char>('0' + clock_minute / 10);
                clk[4] = static_cast<char>('0' + clock_minute % 10);
                clk[5] = '\0';
            }
            const Rect clk_box = clock_rect();
            paint_label(canvas, clk_box.right() - clock_w + 4,
                        label_top(clk_box), hcpp::StringView{clk, 5},
                        theme.card_fg, 13);
        }

        void paint_menu(Canvas& canvas, const Theme& theme,
                        const Rect& menu) noexcept {
            rui_panel(canvas, menu, rui_elev_panel);
            const Rect head{menu.x + 1, menu.y + menu_pad, menu.w - 2,
                            menu_head_h};
            canvas.fill_rect(
                head, canvas.color(
                          rui_lift(theme.card, rui_elev_panel)));
            canvas.fill_circle(head.x + 12, head.y + head.h / 2, 4,
                               canvas.color(theme.accent));
            paint_label(canvas, head.x + 24, label_top(head), "HypeAI OS",
                        theme.muted_fg, 13);
            const hype::hxrt::HxerHeader* const* list =
                hype::hxrt::hxrt_list();
            const hcpp::u32 rows = menu_count > 0 ? menu_count : 1;
            for (hcpp::u32 i = 0; i < rows; ++i) {
                const Rect row{menu.x + 1,
                               head.bottom() +
                                   static_cast<hcpp::i32>(i) * menu_item_h,
                               menu.w - 2, menu_item_h};
                canvas.fill_rect(
                    row, canvas.color(
                             rui_lift(theme.card, rui_elev_panel)));
                const hcpp::i32 cy = row.y + row.h / 2;
                if (menu_count == 0) {
                    paint_label(canvas, row.x + 10, label_top(row), "No apps",
                                theme.muted_fg, 13);
                    continue;
                }
                const hype::hxrt::HxerHeader* app = list[menu_order[i]];
                const Rect icon{row.x + 8, cy - 7, 14, 14};
                rui_rounded_gradient(
                    canvas, icon, 4, rui_lift(theme.primary, 1),
                    theme.primary);
                char mono[2] = {upper(app->name[0]), '\0'};
                paint_label(canvas, icon.x + 4, icon.y + 1,
                            hcpp::StringView{mono, 1}, theme.primary_fg, 11);
                paint_label(canvas, row.x + 30, label_top(row),
                            hcpp::StringView{app->name}, theme.card_fg, 13);
            }
            if (rui_dark_surface()) {
                hype::gfx::gfx_inner_glow_edge(
                    canvas, menu.inset(Insets::uniform(1)),
                    static_cast<hcpp::i32>(theme.radius_lg), theme.accent);
            }
        }

        void print_clock() noexcept {
            serial::write("tray clock=");
            serial::write_char(static_cast<char>('0' + clock_hour / 10));
            serial::write_char(static_cast<char>('0' + clock_hour % 10));
            serial::write_char(':');
            serial::write_char(static_cast<char>('0' + clock_minute / 10));
            serial::write_char(static_cast<char>('0' + clock_minute % 10));
            serial::write("\r\n");
        }

    }

    void deskbar_idle() noexcept {
        ++idle_ticks;
        if (!clock_announced || (idle_ticks & rtc_poll_mask) == 0) {
            hype::time::RtcTime now{};
            if (hype::time::rtc_now(now)) {
                const bool changed =
                    now.hour != clock_hour || now.minute != clock_minute;
                clock_hour = now.hour;
                clock_minute = now.minute;
                if (!clock_announced) {
                    clock_announced = true;
                    print_clock();
                }
                if (changed) {
                    invalidate(clock_rect());
                }
            }
        }
        hcpp::u32 snap[hype::win::wm_max_windows];
        hcpp::u32 n = 0;
        for (; n < hype::win::wm_max_windows; ++n) {
            const Win* w = hype::win::wm_window_at(n);
            if (w == nullptr) {
                break;
            }
            snap[n] = w->id | (static_cast<hcpp::u32>(w->state) << 8);
        }
        bool dirty = n != win_snap_count;
        for (hcpp::u32 i = 0; !dirty && i < n; ++i) {
            dirty = snap[i] != win_snap[i];
        }
        if (dirty) {
            for (hcpp::u32 i = 0; i < n; ++i) {
                win_snap[i] = snap[i];
            }
            win_snap_count = n;
            invalidate(bar);
        }
    }

    void deskbar_paint(hcpp::graphics::Framebuffer& fb, Rect clip) noexcept {
        if (!fb.valid()) {
            return;
        }
        const Rect screen{0, 0, static_cast<hcpp::i32>(fb.width()),
                          static_cast<hcpp::i32>(fb.height())};
        bar = {screen.x, screen.bottom() - deskbar_height, screen.w,
               deskbar_height};
        start_btn = {bar.x + bar_pad, bar.y + bar_pad, start_w,
                     bar.h - bar_pad * 2};
        const Rect menu = compute_menu();
        const Rect area = bar.intersection(clip);
        const Rect menu_area = menu.intersection(clip);
        if (area.empty() && menu_area.empty()) {
            return;
        }
        Canvas canvas(fb);
        canvas.set_clip(clip);
        const Theme& theme = theme_active();
        if (!area.empty()) {
            hype::gfx::gfx_gradient_v(canvas, bar,
                                      rui_lift(theme.card, 2), theme.card);
            canvas.fill_rect(Rect{bar.x, bar.y, bar.w, 1},
                             canvas.color(theme.border));
            paint_start_button(canvas, theme);
            const hcpp::i32 limit = tray_rect().x - task_gap;
            for (hcpp::u32 i = 0; i < hype::win::wm_max_windows; ++i) {
                const Win* w = hype::win::wm_window_at(i);
                if (w == nullptr) {
                    break;
                }
                const Rect btn = task_btn_rect(i);
                if (btn.right() > limit) {
                    break;
                }
                paint_task_button(canvas, theme, btn, *w);
            }
            paint_tray(canvas, theme);
        }
        if (!menu_area.empty()) {
            paint_menu(canvas, theme, menu);
        }
        if (!painted_once) {
            painted_once = true;
            serial::write_line("deskbar ok");
        }
    }

    bool deskbar_handle_mouse(hcpp::i32 x, hcpp::i32 y,
                              bool pressed) noexcept {
        if (!pressed) {
            return false;
        }
        const Point p{x, y};
        if (start_open) {
            const hcpp::i32 item = menu_item_at(p);
            if (item >= 0) {
                const hype::hxrt::HxerHeader* const* list =
                    hype::hxrt::hxrt_list();
                const hype::hxrt::HxerHeader* app =
                    list[menu_order[item]];
                if (app != nullptr) {
                    hype::win::desk_open_app(app->name);
                }
            }
            deskbar_close_start();
            return true;
        }
        if (bar.empty() || !bar.contains(p)) {
            return false;
        }
        if (start_btn.contains(p)) {
            open_start();
            return true;
        }
        const hcpp::i32 limit = tray_rect().x - task_gap;
        for (hcpp::u32 i = 0; i < hype::win::wm_max_windows; ++i) {
            const Win* w = hype::win::wm_window_at(i);
            if (w == nullptr) {
                break;
            }
            const Rect btn = task_btn_rect(i);
            if (btn.right() > limit) {
                break;
            }
            if (!btn.contains(p)) {
                continue;
            }
            if (w->state == WinState::Focused) {
                hype::win::wm_set_minimized(w->id, true);
            } else {
                hype::win::wm_set_minimized(w->id, false);
                hype::win::wm_focus(w->id);
            }
            invalidate(bar);
            return true;
        }
        return true;
    }

    bool deskbar_start_open() noexcept { return start_open; }

    void deskbar_close_start() noexcept {
        if (!start_open) {
            return;
        }
        const Rect gone = compute_menu();
        start_open = false;
        serial::write_line("start menu=close");
        invalidate(gone.united(start_btn));
    }

}
