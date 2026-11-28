#include "kernel/ui/deskbar.h"

#include "kernel/audio/pullmix.h"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/input/curstat.h"
#include "kernel/input/ptrmv.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"
#include "kernel/time/rtc.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/toast.h"
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
        constexpr hcpp::i32 tray_w = 128;
        constexpr hcpp::i32 clock_w = 44;
        // Tray cell layout (offsets inside tray_rect): speaker, net bars,
        // notification bell (with unread badge), then the right-anchored
        // clock cell.
        constexpr hcpp::i32 tray_speaker_x = 2;
        constexpr hcpp::i32 tray_net_x = 20;
        constexpr hcpp::i32 tray_bell_x = 38;
        constexpr hcpp::i32 tray_cell_w = 18;
        constexpr hcpp::i32 tray_bell_w = 24;
        // Quick-settings flyout (bottom-right anchored above the bar).
        constexpr hcpp::i32 qs_w = 300;
        constexpr hcpp::i32 qs_h = 220;
        constexpr hcpp::i32 qs_pad = 12;
        constexpr hcpp::i32 qs_head_h = 24;
        constexpr hcpp::i32 qs_row_h = 36;
        constexpr hcpp::i32 qs_net_h = 28;
        constexpr hcpp::i32 qs_btn_h = 30;
        constexpr hcpp::i32 qs_anim_ticks = 6;
        constexpr hcpp::i32 qs_anim_px = 6;  // px of slide per tick
        constexpr hcpp::i32 toast_col_w = 352;
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

        // Quick-settings state.
        bool qs_open = false;
        hcpp::u32 qs_age = qs_anim_ticks;
        hcpp::u8 qs_drag = 0;  // 0 none, 1 volume, 2 brightness
        hcpp::i32 qs_vol = -1;      // 0..100, -1 = sync from pullmix on open
        hcpp::i32 qs_bright = 75;   // stub: visual + marker only

        // Shell toasts + bell unread badge. ToastHost stores StringViews,
        // so the text lives in this fixed ring while a card is alive.
        struct ToastText {
            char title[32];
            char msg[96];
        };
        ToastHost toasts;
        ToastText toast_text[ToastHost::max_toasts];
        hcpp::u32 toast_next = 0;
        hcpp::u32 unread = 0;
        hcpp::u32 net_last_ip = 0;
        bool net_ip_seen = false;

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

        Rect speaker_zone() noexcept {
            const Rect tray = tray_rect();
            return {tray.x + tray_speaker_x, tray.y, tray_cell_w, tray.h};
        }

        Rect net_zone() noexcept {
            const Rect tray = tray_rect();
            return {tray.x + tray_net_x, tray.y, tray_cell_w, tray.h};
        }

        Rect bell_zone() noexcept {
            const Rect tray = tray_rect();
            return {tray.x + tray_bell_x, tray.y, tray_bell_w, tray.h};
        }

        // Column above the taskbar where toast cards stack; invalidated as
        // one rect so slide-in motion never leaves stale pixels. Grown by
        // the card shadow spill (left + down): the bottom card's soft
        // shadow reaches a few px onto the bar band and must be repainted
        // too or a faint shadow ghost survives toast expiry.
        Rect toast_region() noexcept {
            if (bar.empty()) {
                return {};
            }
            return {bar.right() - toast_col_w - rui_shadow_spill, 0,
                    toast_col_w + rui_shadow_spill,
                    bar.y - 2 + rui_shadow_spill_down};
        }

        // Quick-settings panel at its final anchor (bottom-right above bar).
        Rect compute_qs() noexcept {
            if (bar.empty()) {
                return {};
            }
            Rect r{bar.right() - bar_pad - qs_w, bar.y - 6 - qs_h, qs_w, qs_h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        // Panel rect at the current slide-in animation position.
        Rect qs_rect_now() noexcept {
            if (!qs_open) {
                return {};
            }
            Rect r = compute_qs();
            const hcpp::u32 left =
                qs_age < qs_anim_ticks ? qs_anim_ticks - qs_age : 0;
            r.y += static_cast<hcpp::i32>(left) * qs_anim_px;
            return r;
        }

        // Damage for the open/close animation: final rect grown downward
        // by the full travel so every intermediate frame is repainted,
        // then grown again by the panel shadow spill on every side.
        Rect qs_anim_region() noexcept {
            Rect r = compute_qs();
            r.h += qs_anim_ticks * qs_anim_px;
            return rui_paint_bounds(r);
        }

        Rect qs_vol_row(const Rect& panel) noexcept {
            return {panel.x + qs_pad, panel.y + qs_pad + qs_head_h,
                    panel.w - 2 * qs_pad, qs_row_h};
        }

        Rect qs_bright_row(const Rect& panel) noexcept {
            const Rect vol = qs_vol_row(panel);
            return {vol.x, vol.bottom(), vol.w, qs_row_h};
        }

        Rect qs_net_row(const Rect& panel) noexcept {
            const Rect br = qs_bright_row(panel);
            return {br.x, br.bottom() + 8, br.w, qs_net_h};
        }

        Rect qs_lock_btn(const Rect& panel) noexcept {
            const hcpp::i32 w = (panel.w - 2 * qs_pad - 8) / 2;
            return {panel.x + qs_pad, panel.bottom() - qs_pad - qs_btn_h, w,
                    qs_btn_h};
        }

        Rect qs_power_btn(const Rect& panel) noexcept {
            const Rect lock = qs_lock_btn(panel);
            return {lock.right() + 8, lock.y, lock.w, lock.h};
        }

        // Slider track + its (fatter) hit zone inside a 36px row.
        Rect qs_track(const Rect& row) noexcept {
            return {row.x + 24, row.y + row.h / 2 - 3, row.w - 24 - 42, 6};
        }

        Rect qs_track_hit(const Rect& row) noexcept {
            return {row.x + 18, row.y, row.w - 18, row.h};
        }

        void copy_text(char* dst, hcpp::size_t cap,
                       const char* src) noexcept {
            hcpp::size_t i = 0;
            if (src != nullptr) {
                while (i + 1 < cap && src[i] != '\0') {
                    dst[i] = src[i];
                    ++i;
                }
            }
            dst[i] = '\0';
        }

        hcpp::size_t fmt_u32(char* dst, hcpp::u32 value) noexcept {
            char tmp[10];
            hcpp::size_t n = 0;
            do {
                tmp[n++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0 && n < sizeof(tmp));
            for (hcpp::size_t i = 0; i < n; ++i) {
                dst[i] = tmp[n - 1 - i];
            }
            dst[n] = '\0';
            return n;
        }

        hcpp::i32 clamp_pct(hcpp::i32 v) noexcept {
            if (v < 0) {
                return 0;
            }
            if (v > 100) {
                return 100;
            }
            return v;
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
            if (qs_open) {
                deskbar_close_qs();
            }
            build_menu_order();
            start_open = true;
            serial::write_line("start menu=open");
            invalidate(rui_paint_bounds(compute_menu()).united(bar));
        }

        void open_qs() noexcept {
            if (qs_open) {
                return;
            }
            // One flyout at a time, like the real shells.
            if (start_open) {
                deskbar_close_start();
            }
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            if (hype::win::wm_ctx_visible()) {
                hype::win::wm_ctx_close();
            }
            if (qs_vol < 0) {
                // First open: adopt the mixer master gain (Q8, 256 = 100%).
                qs_vol = clamp_pct(
                    (static_cast<hcpp::i32>(
                         hype::audio::pullmix_master_gain()) *
                     100) /
                    256);
            }
            qs_open = true;
            qs_age = 0;
            serial::write_line("tray qs=open");
            invalidate(qs_anim_region().united(speaker_zone()));
        }

        hcpp::i32 slider_pct_at(hcpp::i32 x, const Rect& row) noexcept {
            const Rect track = qs_track(row);
            if (track.w <= 0) {
                return 0;
            }
            return clamp_pct((x - track.x) * 100 / track.w);
        }

        void apply_volume(hcpp::i32 pct) noexcept {
            pct = clamp_pct(pct);
            if (pct == qs_vol) {
                return;
            }
            qs_vol = pct;
            // pullmix master gain is Q8: 256 == unity (100%).
            hype::audio::pullmix_set_master_gain(
                static_cast<hcpp::u16>(pct * 256 / 100));
            serial::write("qs vol=");
            serial::write_dec32(static_cast<hcpp::u32>(pct));
            serial::write("\r\n");
            invalidate(qs_vol_row(qs_rect_now()));
        }

        void apply_bright(hcpp::i32 pct) noexcept {
            pct = clamp_pct(pct);
            if (pct == qs_bright) {
                return;
            }
            qs_bright = pct;
            serial::write("qs bright=");
            serial::write_dec32(static_cast<hcpp::u32>(pct));
            serial::write("\r\n");
            invalidate(qs_bright_row(qs_rect_now()));
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
            // Long app names clip at the button edge instead of bleeding
            // glyphs into the neighbouring task button.
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(btn));
            paint_label(canvas, btn.x + 10, label_top(btn),
                        hcpp::StringView{w.app_name},
                        minimized ? theme.muted_fg : theme.card_fg, 13);
            canvas.set_clip(saved_clip);
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

        void paint_speaker_glyph(Canvas& canvas, hcpp::i32 gx,
                                 hcpp::i32 cy, hcpp::u32 glyph) noexcept {
            canvas.fill_rect(Rect{gx, cy - 3, 4, 6}, glyph);
            canvas.draw_line(gx + 4, cy - 2, gx + 9, cy - 6, glyph);
            canvas.draw_line(gx + 4, cy + 2, gx + 9, cy + 6, glyph);
            canvas.draw_line(gx + 9, cy - 6, gx + 9, cy + 6, glyph);
            canvas.draw_line(gx + 11, cy - 2, gx + 12, cy, glyph);
            canvas.draw_line(gx + 12, cy, gx + 11, cy + 2, glyph);
        }

        void paint_net_glyph(Canvas& canvas, hcpp::i32 gx, hcpp::i32 cy,
                             hcpp::u32 glyph, bool online) noexcept {
            const hcpp::i32 bars = online ? 4 : 1;
            for (hcpp::i32 k = 0; k < 4; ++k) {
                const hcpp::i32 h = (k + 1) * 2;
                canvas.fill_rect(
                    Rect{gx + k * 4, cy + 5 - h, 3, h},
                    k < bars ? glyph : canvas.color(theme_active().muted));
            }
        }

        void paint_bell_glyph(Canvas& canvas, const Theme& theme,
                              const Rect& zone) noexcept {
            const hcpp::i32 cx = zone.x + 9;
            const hcpp::i32 cy = zone.y + zone.h / 2;
            const hcpp::u32 glyph = canvas.color(theme.muted_fg);
            // Dome + skirt + clapper.
            canvas.fill_circle(cx, cy - 2, 4, glyph);
            canvas.fill_rect(Rect{cx - 5, cy - 1, 10, 5}, glyph);
            canvas.fill_rect(Rect{cx - 6, cy + 4, 12, 2}, glyph);
            canvas.fill_rect(Rect{cx - 1, cy + 7, 3, 2}, glyph);
            if (unread == 0) {
                return;
            }
            // Unread badge: destructive dot with a single-digit count.
            const hcpp::i32 bx = zone.right() - 6;
            const hcpp::i32 by = zone.y + 6;
            canvas.fill_circle(bx, by, 6, canvas.color(theme.destructive));
            char digit[2] = {
                static_cast<char>('0' + (unread > 9 ? 9 : unread)), '\0'};
            paint_label(canvas, bx - 3, by - 6, hcpp::StringView{digit, 1},
                        theme.primary_fg, 11);
        }

        void paint_tray(Canvas& canvas, const Theme& theme) noexcept {
            const hcpp::u32 glyph = canvas.color(theme.muted_fg);
            const Rect spk = speaker_zone();
            paint_speaker_glyph(canvas, spk.x + 3, spk.y + spk.h / 2, glyph);
            const Rect net = net_zone();
            paint_net_glyph(canvas, net.x + 1, net.y + net.h / 2, glyph,
                            hype::net::net_ip() != 0);
            paint_bell_glyph(canvas, theme, bell_zone());
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
            // Clock glyphs stay inside the clock cell — no bleed into the
            // bell badge or the bar edge.
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(clk_box));
            paint_label(canvas, clk_box.right() - clock_w + 4,
                        label_top(clk_box), hcpp::StringView{clk, 5},
                        theme.card_fg, 13);
            canvas.set_clip(saved_clip);
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

        void paint_slider(Canvas& canvas, const Theme& theme,
                          const Rect& row, hcpp::i32 pct) noexcept {
            const Rect track = qs_track(row);
            const hcpp::i32 cy = track.y + track.h / 2;
            const hcpp::i32 fill = track.w * clamp_pct(pct) / 100;
            canvas.fill_rounded_rect(track, 3, canvas.color(theme.muted));
            if (fill > 0) {
                canvas.fill_rounded_rect(
                    Rect{track.x, track.y, fill, track.h}, 3,
                    canvas.color(theme.accent));
            }
            const hcpp::i32 kx = track.x + fill;
            canvas.fill_circle(kx, cy, 6, canvas.color(theme.accent));
            canvas.fill_circle(kx, cy, 3,
                               canvas.color(rui_lift(theme.accent, 2)));
            char pct_text[5];
            const hcpp::size_t n =
                fmt_u32(pct_text, static_cast<hcpp::u32>(clamp_pct(pct)));
            pct_text[n] = '%';
            pct_text[n + 1] = '\0';
            paint_label(canvas, row.right() - 36, label_top(row),
                        hcpp::StringView{pct_text}, theme.card_fg, 13);
        }

        void paint_sun_glyph(Canvas& canvas, const Theme& theme,
                             const Rect& row) noexcept {
            const hcpp::i32 cx = row.x + 9;
            const hcpp::i32 cy = row.y + row.h / 2;
            const hcpp::u32 glyph = canvas.color(theme.muted_fg);
            canvas.fill_circle(cx, cy, 4, glyph);
            canvas.draw_line(cx - 8, cy, cx - 6, cy, glyph);
            canvas.draw_line(cx + 6, cy, cx + 8, cy, glyph);
            canvas.draw_line(cx, cy - 8, cx, cy - 6, glyph);
            canvas.draw_line(cx, cy + 6, cx, cy + 8, glyph);
        }

        void paint_qs(Canvas& canvas, const Theme& theme,
                      const Rect& panel) noexcept {
            rui_panel(canvas, panel, rui_elev_panel);
            const Rect head{panel.x + qs_pad, panel.y + qs_pad,
                            panel.w - 2 * qs_pad, qs_head_h};
            canvas.fill_circle(head.x + 8, head.y + head.h / 2, 4,
                               canvas.color(theme.accent));
            paint_label(canvas, head.x + 20, label_top(head),
                        "Quick settings", theme.card_fg, 13);

            const Rect vol = qs_vol_row(panel);
            paint_speaker_glyph(canvas, vol.x + 4, vol.y + vol.h / 2,
                                canvas.color(theme.muted_fg));
            paint_slider(canvas, theme, vol, qs_vol < 0 ? 100 : qs_vol);

            const Rect br = qs_bright_row(panel);
            paint_sun_glyph(canvas, theme, br);
            paint_slider(canvas, theme, br, qs_bright);

            // Divider, then the Network status row.
            canvas.fill_rect(
                Rect{panel.x + qs_pad, br.bottom() + 3,
                     panel.w - 2 * qs_pad, 1},
                canvas.color(theme.border));
            const Rect net = qs_net_row(panel);
            paint_net_glyph(canvas, net.x + 1, net.y + net.h / 2,
                            canvas.color(theme.muted_fg),
                            hype::net::net_ip() != 0);
            paint_label(canvas, net.x + 24, label_top(net), "Network",
                        theme.card_fg, 13);
            char status[24];
            const hcpp::u32 ip = hype::net::net_ip();
            if (ip != 0) {
                hcpp::size_t o = 0;
                for (hcpp::i32 i = 0; i < 4; ++i) {
                    if (i != 0) {
                        status[o++] = '.';
                    }
                    o += fmt_u32(status + o, (ip >> (24 - i * 8)) & 0xFFu);
                }
                status[o] = '\0';
            } else {
                copy_text(status, sizeof(status),
                          hype::net::net_ready() ? "connecting" : "offline");
            }
            paint_label(canvas, net.right() - 88, label_top(net),
                        hcpp::StringView{status}, theme.muted_fg, 13);

            // Lock / Power buttons (no session-lock or ACPI API yet — the
            // actions report through serial markers).
            const Rect lock = qs_lock_btn(panel);
            const Rect power = qs_power_btn(panel);
            rui_control_bg(canvas, lock, theme.secondary, theme.border);
            paint_label(canvas, lock.x + (lock.w - 24) / 2, label_top(lock),
                        "Lock", theme.card_fg, 13);
            rui_control_bg(canvas, power, theme.secondary,
                           theme.destructive);
            paint_label(canvas, power.x + (power.w - 36) / 2,
                        label_top(power), "Power", theme.card_fg, 13);

            if (rui_dark_surface()) {
                hype::gfx::gfx_inner_glow_edge(
                    canvas, panel.inset(Insets::uniform(1)),
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
        // Slider drags: the WM only delivers press edges to the bar, so the
        // grab is tracked here from raw pointer state while the button
        // stays held.
        if (qs_drag != 0) {
            if ((hype::input::ptr_buttons() &
                 hype::input::ptr_button_left) != 0) {
                const Rect panel = qs_rect_now();
                const Rect row = qs_drag == 1 ? qs_vol_row(panel)
                                              : qs_bright_row(panel);
                const hcpp::i32 pct =
                    slider_pct_at(hype::input::ptr_x(), row);
                if (qs_drag == 1) {
                    apply_volume(pct);
                } else {
                    apply_bright(pct);
                }
            } else {
                qs_drag = 0;
            }
        }
        // Panel slide-in: repaint the grown rect each tick so the vacated
        // sliver below the moving panel is never left stale.
        if (qs_open && qs_age < qs_anim_ticks) {
            ++qs_age;
            invalidate(qs_anim_region());
        }
        // Toast TTL counts down inside ToastHost::paint — keep the stack
        // column damaged while any card is alive so expiry and the
        // slide-in both animate.
        if (toasts.count() > 0) {
            invalidate(toast_region());
        }
        // DHCP watch: one toast per new lease. The first observation only
        // seeds the baseline so a pre-login lease doesn't fire at boot.
        const hcpp::u32 ip = hype::net::net_ip();
        if (!net_ip_seen) {
            net_last_ip = ip;
            net_ip_seen = true;
        } else if (ip != net_last_ip) {
            net_last_ip = ip;
            if (ip != 0) {
                char msg[32] = "ip=";
                hcpp::size_t o = 3;
                for (hcpp::i32 i = 0; i < 4; ++i) {
                    if (i != 0) {
                        msg[o++] = '.';
                    }
                    o += fmt_u32(msg + o, (ip >> (24 - i * 8)) & 0xFFu);
                }
                msg[o] = '\0';
                deskbar_notify("Network connected", msg);
            }
        }
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
        const Rect qs = qs_rect_now();
        const Rect area = bar.intersection(clip);
        const Rect menu_area = menu.intersection(clip);
        const Rect qs_area = qs.intersection(clip);
        const Rect toast_area = toasts.count() > 0
                                    ? toast_region().intersection(clip)
                                    : Rect{};
        if (area.empty() && menu_area.empty() && qs_area.empty() &&
            toast_area.empty()) {
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
        if (!qs_area.empty()) {
            paint_qs(canvas, theme, qs);
        }
        // Toasts sit in the band above the bar, topmost of the shell UI.
        if (toasts.count() > 0 && !toast_region().empty()) {
            const Rect band{screen.x, screen.y, screen.w, bar.y - 2};
            toasts.paint(canvas, band);
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
        const bool was_qs = qs_open;
        if (qs_open) {
            const Rect panel = qs_rect_now();
            if (panel.contains(p)) {
                if (qs_track_hit(qs_vol_row(panel)).contains(p)) {
                    qs_drag = 1;
                    apply_volume(
                        slider_pct_at(p.x, qs_vol_row(panel)));
                    return true;
                }
                if (qs_track_hit(qs_bright_row(panel)).contains(p)) {
                    qs_drag = 2;
                    apply_bright(
                        slider_pct_at(p.x, qs_bright_row(panel)));
                    return true;
                }
                if (qs_lock_btn(panel).contains(p)) {
                    serial::write_line("power action=lock");
                    deskbar_close_qs();
                    return true;
                }
                if (qs_power_btn(panel).contains(p)) {
                    serial::write_line("power action=shutdown");
                    deskbar_close_qs();
                    return true;
                }
                return true;
            }
            // Click outside dismisses the flyout, then the same click is
            // free to hit the bar/tray zones below.
            deskbar_close_qs();
        }
        if (bar.empty() || !bar.contains(p)) {
            return false;
        }
        if (start_btn.contains(p)) {
            open_start();
            return true;
        }
        if (tray_rect().contains(p)) {
            if (speaker_zone().contains(p) || net_zone().contains(p)) {
                if (!was_qs) {
                    open_qs();
                }
                return true;
            }
            if (bell_zone().contains(p)) {
                if (unread != 0) {
                    unread = 0;
                    serial::write_line("tray unread=0");
                    invalidate(bell_zone());
                }
                return true;
            }
            // Clock cell / padding: consume without action.
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
                deskbar_notify("Minimized", w->app_name);
            } else {
                hype::win::wm_set_minimized(w->id, false);
                hype::win::wm_focus(w->id);
            }
            invalidate(bar);
            return true;
        }
        return true;
    }

    void deskbar_notify(const char* title, const char* msg) noexcept {
        ToastText& slot = toast_text[toast_next];
        toast_next = (toast_next + 1) %
                     static_cast<hcpp::u32>(ToastHost::max_toasts);
        copy_text(slot.title, sizeof(slot.title), title);
        copy_text(slot.msg, sizeof(slot.msg), msg);
        if (toasts.count() >= ToastHost::max_toasts) {
            toasts.dismiss(0);
        }
        toasts.show(hcpp::StringView{slot.msg}, ToastKind::Info,
                    hcpp::StringView{slot.title});
        ++unread;
        serial::write("toast push=");
        serial::write(slot.title);
        serial::write("\r\n");
        serial::write("tray unread=");
        serial::write_dec32(unread);
        serial::write("\r\n");
        invalidate(toast_region().united(bell_zone()));
    }

    bool deskbar_qs_open() noexcept { return qs_open; }

    void deskbar_close_qs() noexcept {
        if (!qs_open) {
            return;
        }
        // Include the panel shadow spill so dismiss leaves no shadow ring.
        const Rect gone = rui_paint_bounds(qs_rect_now());
        qs_open = false;
        qs_drag = 0;
        serial::write_line("tray qs=close");
        invalidate(gone.united(speaker_zone()));
    }

    bool deskbar_start_open() noexcept { return start_open; }

    void deskbar_open_start() noexcept {
        if (!start_open) {
            open_start();
        }
    }

    void deskbar_close_start() noexcept {
        if (!start_open) {
            return;
        }
        // Include the panel shadow spill so no shadow ring survives the
        // dismissed start menu.
        const Rect gone = rui_paint_bounds(compute_menu());
        start_open = false;
        serial::write_line("start menu=close");
        invalidate(gone.united(start_btn));
    }

}
