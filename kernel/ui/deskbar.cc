#include "kernel/ui/deskbar.h"

#include "kernel/audio/pullmix.h"
#include "kernel/cfg/settings.h"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/apps/builtins.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/input/curstat.h"
#include "kernel/input/ptrmv.h"
#include "kernel/net/net.h"
#include "kernel/sched/watchdog.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/time/rtc.h"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/clipboard.h"
#include "kernel/ui/huitokens.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/toast.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/vfs/hypefs.h"
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
        // Virtual-desktop strip (issue #135): one numbered square per
        // desktop immediately left of the tray; the current desktop is
        // accent-highlighted and the squares are clickable switches.
        constexpr hcpp::i32 vd_cell_w = 16;
        constexpr hcpp::i32 vd_gap = 3;
        constexpr hcpp::i32 vd_pad = 6;
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
        // Notification-center flyout (bottom-right anchored above the bar,
        // same slide-in machinery as quick settings).
        constexpr hcpp::i32 nc_w = 320;
        constexpr hcpp::i32 nc_h = 400;
        constexpr hcpp::i32 nc_pad = 12;
        constexpr hcpp::i32 nc_head_h = 26;
        constexpr hcpp::i32 nc_row_h = 46;
        constexpr hcpp::i32 nc_anim_ticks = 6;
        constexpr hcpp::i32 nc_anim_px = 6;
        constexpr hcpp::u32 hist_max = 24;
        // Clipboard-history flyout (Win+V): bottom-right anchored above
        // the bar, same slide-in machinery as quick settings.
        constexpr hcpp::i32 cv_w = 320;
        constexpr hcpp::i32 cv_pad = 12;
        constexpr hcpp::i32 cv_head_h = 26;
        constexpr hcpp::i32 cv_row_h = 30;
        constexpr hcpp::u32 cv_max_rows = 10;
        constexpr hcpp::i32 cv_anim_ticks = 6;
        constexpr hcpp::i32 cv_anim_px = 6;
        constexpr hcpp::u32 cv_preview_max = 40;
        // Calendar + clock flyout (issue #132): tray-clock click toggles a
        // bottom-right-anchored panel with a big clock, the full date and
        // a navigable month grid (same slide-in machinery as quick
        // settings).
        constexpr hcpp::i32 cal_w = 280;
        constexpr hcpp::i32 cal_h = 300;
        constexpr hcpp::i32 cal_pad = 12;
        constexpr hcpp::i32 cal_clock_h = 30;   // big "HH:MM" line
        constexpr hcpp::i32 cal_date_h = 18;    // full-date line under it
        constexpr hcpp::i32 cal_nav_h = 26;     // "< Month YYYY >" row
        constexpr hcpp::i32 cal_wd_h = 16;      // weekday header row
        constexpr hcpp::i32 cal_cell_h = 28;    // one month-grid row
        constexpr hcpp::i32 cal_anim_ticks = 6;
        constexpr hcpp::i32 cal_anim_px = 6;
        constexpr hcpp::i32 cal_nav_max = 120;  // months either direction
        constexpr hcpp::i32 toast_col_w = 352;
        constexpr hcpp::i32 menu_w = 180;
        constexpr hcpp::i32 menu_pad = 4;
        constexpr hcpp::i32 menu_item_h = 24;
        constexpr hcpp::i32 menu_head_h = 24;
        constexpr hcpp::u32 menu_max_items = 12;
        // Start-menu search (issue #129): auto-focused field on top, live
        // app-name filter + a bounded 1-level file walk under U:/.
        constexpr hcpp::i32 menu_search_h = 26;
        constexpr hcpp::i32 menu_search_gap = 6;
        constexpr hcpp::i32 menu_section_h = 18;
        constexpr hcpp::u32 search_cap = 31;
        constexpr hcpp::u32 search_files_max = 8;
        constexpr hcpp::u32 search_list_cap = 48;
        constexpr hcpp::u32 search_scan_max = 128;
        constexpr const char* search_root = "HYPEOS://U";
        // Set-1 scancodes routed through deskbar_handle_key.
        constexpr hcpp::u16 key_escape = 1;
        constexpr hcpp::u16 key_backspace = 14;
        constexpr hcpp::u16 key_enter = 28;
        constexpr hcpp::u16 key_up = 72;
        constexpr hcpp::u16 key_left = 75;
        constexpr hcpp::u16 key_right = 77;
        constexpr hcpp::u16 key_down = 80;
        constexpr hcpp::u32 rtc_poll_mask = 31;

        Rect bar{};
        Rect start_btn{};
        hcpp::u32 menu_order[hype::hxrt::hxrt_max_apps];
        hcpp::u32 menu_count = 0;
        bool start_open = false;

        // Start-menu search state. `menu_order`/`menu_count` hold the
        // *filtered* app list whenever the query is non-empty; file hits
        // sit in the fixed FileHit pool and are informational-or-launch
        // rows painted under an "Files" section.
        struct FileHit {
            char name[hype::vfs::fs_name_size];
            char path[hype::vfs::hypepath_max_length];
            hcpp::u32 flags;
        };
        char search_query[search_cap + 1]{};
        hcpp::u32 search_len = 0;
        FileHit file_hits[search_files_max];
        hcpp::u32 file_hits_count = 0;
        // Highlighted result row across the combined Apps+Files list
        // (Enter activates it; defaults to the top hit).
        hcpp::u32 search_sel = 0;
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

        // Notification-center state. The history ring is the archive of
        // every deskbar_notify() push, independent of the transient toast
        // cards: toasts expire, history rows persist until dismissed.
        struct HistEntry {
            char title[32];
            char msg[96];
            hcpp::u64 timestamp_ms;  // uptime_ms() at push
            hcpp::u8 hour;           // RTC wall clock at push (0xFF = n/a)
            hcpp::u8 minute;
            ToastKind kind;
        };
        HistEntry hist[hist_max];
        hcpp::u32 hist_head = 0;   // next write slot
        hcpp::u32 hist_count = 0;  // live entries (<= hist_max)
        bool nc_open = false;
        hcpp::u32 nc_age = nc_anim_ticks;

        // Clipboard-history flyout state; the entries themselves live in
        // kernel/ui/clipboard.cc (clip_count/clip_history/clip_paste_at).
        bool cv_open = false;
        hcpp::u32 cv_age = cv_anim_ticks;

        // Calendar flyout state: `cal_view_*` is the month on screen,
        // `cal_nav` the signed offset in months from the real RTC month
        // (bounded by cal_nav_max so the grid can never run away).
        bool cal_open = false;
        hcpp::u32 cal_age = cal_anim_ticks;
        hcpp::i32 cal_nav = 0;
        hcpp::i32 cal_view_y = 2026;
        hcpp::u8 cal_view_m = 1;

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

        // Desktop-strip band: vd_pad gap left of the tray, one cell per
        // desktop. Task buttons must stop at the strip's left edge.
        Rect vdesk_strip() noexcept {
            const hcpp::i32 n =
                static_cast<hcpp::i32>(hype::win::vdesk_count());
            const hcpp::i32 w = n * vd_cell_w + (n - 1) * vd_gap;
            const Rect tray = tray_rect();
            return {tray.x - vd_pad - w, tray.y, w, tray.h};
        }

        Rect vdesk_cell(hcpp::u32 index) noexcept {
            const Rect strip = vdesk_strip();
            return {strip.x +
                        static_cast<hcpp::i32>(index) * (vd_cell_w + vd_gap),
                    strip.y + 1, vd_cell_w, strip.h - 2};
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

        // Notification-center panel at its final anchor.
        Rect compute_nc() noexcept {
            if (bar.empty()) {
                return {};
            }
            Rect r{bar.right() - bar_pad - nc_w, bar.y - 6 - nc_h, nc_w,
                   nc_h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        Rect nc_rect_now() noexcept {
            if (!nc_open) {
                return {};
            }
            Rect r = compute_nc();
            const hcpp::u32 left =
                nc_age < nc_anim_ticks ? nc_anim_ticks - nc_age : 0;
            r.y += static_cast<hcpp::i32>(left) * nc_anim_px;
            return r;
        }

        Rect nc_anim_region() noexcept {
            Rect r = compute_nc();
            r.h += nc_anim_ticks * nc_anim_px;
            return rui_paint_bounds(r);
        }

        Rect nc_clear_btn(const Rect& panel) noexcept {
            return {panel.right() - nc_pad - 52,
                    panel.y + nc_pad - 2, 52, nc_head_h + 4};
        }

        // Scrollable-looking list band under the header; only the newest
        // rows that fit are painted (no scroll offset yet).
        Rect nc_list(const Rect& panel) noexcept {
            const hcpp::i32 top = panel.y + nc_pad + nc_head_h + 6;
            return {panel.x + 6, top, panel.w - 12,
                    panel.bottom() - nc_pad - top};
        }

        Rect nc_row_rect(const Rect& panel, hcpp::u32 index) noexcept {
            const Rect list = nc_list(panel);
            return {list.x + 2,
                    list.y + static_cast<hcpp::i32>(index) * nc_row_h,
                    list.w - 4, nc_row_h - 4};
        }

        hcpp::u32 nc_rows_fit(const Rect& panel) noexcept {
            const hcpp::i32 fit = nc_list(panel).h / nc_row_h;
            hcpp::u32 rows = hist_count;
            if (rows > static_cast<hcpp::u32>(fit < 0 ? 0 : fit)) {
                rows = static_cast<hcpp::u32>(fit < 0 ? 0 : fit);
            }
            return rows;
        }

        // Small "x" dismiss hitbox at a row's top-right corner.
        Rect nc_row_x(const Rect& row) noexcept {
            return {row.right() - 20, row.y + 4, 16, 16};
        }

        // Ring slot of the entry shown at `visible` (0 = newest).
        hcpp::u32 hist_slot(hcpp::u32 visible) noexcept {
            return (hist_head + hist_max - 1 - visible) % hist_max;
        }

        // Drops the entry at `visible`; older entries shift one slot
        // newer. The head is unchanged so append order is preserved.
        void hist_remove(hcpp::u32 visible) noexcept {
            if (visible >= hist_count) {
                return;
            }
            for (hcpp::u32 j = visible + 1; j < hist_count; ++j) {
                hist[hist_slot(j - 1)] = hist[hist_slot(j)];
            }
            --hist_count;
        }

        hcpp::u32 nc_kind_rgb(ToastKind kind, const Theme& theme) noexcept {
            switch (kind) {
                case ToastKind::Success:
                    return theme.success;
                case ToastKind::Warning:
                    return theme.warning;
                case ToastKind::Error:
                    return theme.destructive;
                case ToastKind::Info:
                default:
                    return theme.primary;
            }
        }

        // ---- clipboard-history flyout (Win+V) ---------------------------

        hcpp::u32 cv_rows() noexcept {
            const hcpp::u32 n = clip_count();
            return n < cv_max_rows ? n : cv_max_rows;
        }

        // Clipboard panel at its final anchor (bottom-right above bar).
        Rect compute_cv() noexcept {
            if (bar.empty()) {
                return {};
            }
            const hcpp::u32 rows = cv_rows();
            const hcpp::i32 h =
                cv_pad * 2 + cv_head_h + 6 +
                static_cast<hcpp::i32>(rows == 0 ? 1 : rows) * cv_row_h;
            Rect r{bar.right() - bar_pad - cv_w, bar.y - 6 - h, cv_w, h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        Rect cv_rect_now() noexcept {
            if (!cv_open) {
                return {};
            }
            Rect r = compute_cv();
            const hcpp::u32 left =
                cv_age < cv_anim_ticks ? cv_anim_ticks - cv_age : 0;
            r.y += static_cast<hcpp::i32>(left) * cv_anim_px;
            return r;
        }

        Rect cv_anim_region() noexcept {
            Rect r = compute_cv();
            r.h += cv_anim_ticks * cv_anim_px;
            return rui_paint_bounds(r);
        }

        Rect cv_row_rect(const Rect& panel, hcpp::u32 index) noexcept {
            const hcpp::i32 top = panel.y + cv_pad + cv_head_h + 6;
            return {panel.x + 6,
                    top + static_cast<hcpp::i32>(index) * cv_row_h,
                    panel.w - 12, cv_row_h - 4};
        }

        // ---- calendar + clock flyout (issue #132) -----------------------

        bool cal_is_leap(hcpp::i32 y) noexcept {
            return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        }

        hcpp::i32 cal_month_days(hcpp::i32 y, hcpp::i32 m) noexcept {
            static const hcpp::u8 days[12] = {31, 28, 31, 30, 31, 30,
                                              31, 31, 30, 31, 30, 31};
            if (m < 1 || m > 12) {
                return 30;
            }
            if (m == 2 && cal_is_leap(y)) {
                return 29;
            }
            return days[m - 1];
        }

        // Serial day number: 1 Jan of year 1 == 1 (a Monday), so the
        // Monday-first weekday index of a date is (day_num - 1) % 7.
        hcpp::u32 cal_day_num(hcpp::i32 y, hcpp::i32 m,
                              hcpp::i32 d) noexcept {
            const hcpp::u32 yy = static_cast<hcpp::u32>(y - 1);
            hcpp::u32 n = yy * 365 + yy / 4 - yy / 100 + yy / 400;
            for (hcpp::i32 i = 1; i < m; ++i) {
                n += static_cast<hcpp::u32>(cal_month_days(y, i));
            }
            return n + static_cast<hcpp::u32>(d);
        }

        // Weekday column, 0 = Monday .. 6 = Sunday.
        hcpp::u32 cal_weekday(hcpp::i32 y, hcpp::i32 m,
                              hcpp::i32 d) noexcept {
            return (cal_day_num(y, m, d) - 1) % 7;
        }

        // Calendar panel at its final anchor (bottom-right above bar).
        Rect compute_cal() noexcept {
            if (bar.empty()) {
                return {};
            }
            Rect r{bar.right() - bar_pad - cal_w, bar.y - 6 - cal_h, cal_w,
                   cal_h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        Rect cal_rect_now() noexcept {
            if (!cal_open) {
                return {};
            }
            Rect r = compute_cal();
            const hcpp::u32 left =
                cal_age < cal_anim_ticks ? cal_anim_ticks - cal_age : 0;
            r.y += static_cast<hcpp::i32>(left) * cal_anim_px;
            return r;
        }

        Rect cal_anim_region() noexcept {
            Rect r = compute_cal();
            r.h += cal_anim_ticks * cal_anim_px;
            return rui_paint_bounds(r);
        }

        // Vertical bands inside the panel, top to bottom.
        Rect cal_clock_row(const Rect& panel) noexcept {
            return {panel.x + cal_pad, panel.y + cal_pad,
                    panel.w - 2 * cal_pad, cal_clock_h};
        }

        Rect cal_date_row(const Rect& panel) noexcept {
            const Rect clk = cal_clock_row(panel);
            return {clk.x, clk.bottom() + 2, clk.w, cal_date_h};
        }

        Rect cal_nav_row(const Rect& panel) noexcept {
            const Rect date = cal_date_row(panel);
            // +8 clears the divider painted 4px under the date line.
            return {date.x, date.bottom() + 8, date.w, cal_nav_h};
        }

        Rect cal_prev_btn(const Rect& panel) noexcept {
            const Rect nav = cal_nav_row(panel);
            return {nav.x, nav.y, 26, nav.h};
        }

        Rect cal_next_btn(const Rect& panel) noexcept {
            const Rect nav = cal_nav_row(panel);
            return {nav.right() - 26, nav.y, 26, nav.h};
        }

        Rect cal_wd_row(const Rect& panel) noexcept {
            const Rect nav = cal_nav_row(panel);
            return {nav.x, nav.bottom() + 2, nav.w, cal_wd_h};
        }

        Rect cal_grid(const Rect& panel) noexcept {
            const Rect wd = cal_wd_row(panel);
            return {wd.x, wd.bottom() + 4, wd.w, 6 * cal_cell_h};
        }

        Rect cal_cell(const Rect& panel, hcpp::u32 row,
                      hcpp::u32 col) noexcept {
            const Rect grid = cal_grid(panel);
            const hcpp::i32 cw = grid.w / 7;
            return {grid.x + static_cast<hcpp::i32>(col) * cw,
                    grid.y + static_cast<hcpp::i32>(row) * cal_cell_h,
                    cw, cal_cell_h};
        }

        // Steps the viewed month by `delta` (bounded by cal_nav_max) and
        // prints the "cal nav=<y-m>" marker with the zero-padded month.
        void cal_nav_month(hcpp::i32 delta) noexcept {
            const hcpp::i32 next = cal_nav + delta;
            if (next < -cal_nav_max || next > cal_nav_max) {
                return;
            }
            cal_nav = next;
            hcpp::i32 m = static_cast<hcpp::i32>(cal_view_m) + delta;
            while (m < 1) {
                m += 12;
                --cal_view_y;
            }
            while (m > 12) {
                m -= 12;
                ++cal_view_y;
            }
            cal_view_m = static_cast<hcpp::u8>(m);
            serial::write("cal nav=");
            serial::write_dec32(static_cast<hcpp::u32>(cal_view_y));
            serial::write_char('-');
            if (cal_view_m < 10) {
                serial::write_char('0');
            }
            serial::write_dec32(cal_view_m);
            serial::write("\r\n");
            invalidate(rui_paint_bounds(cal_rect_now()));
        }

        void open_cal() noexcept {
            if (cal_open) {
                return;
            }
            // One flyout at a time, like the real shells.
            if (start_open) {
                deskbar_close_start();
            }
            if (qs_open) {
                deskbar_close_qs();
            }
            if (nc_open) {
                deskbar_close_nc();
            }
            if (cv_open) {
                deskbar_close_clip();
            }
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            if (hype::win::wm_ctx_visible()) {
                hype::win::wm_ctx_close();
            }
            // Anchor the grid on the real RTC month; keep the last view
            // as fallback when the RTC cannot be read.
            cal_nav = 0;
            hype::time::RtcTime now{};
            if (hype::time::rtc_now(now)) {
                cal_view_y = now.year;
                cal_view_m = now.month;
            }
            cal_open = true;
            cal_age = 0;
            serial::write_line("cal open=ok");
            invalidate(cal_anim_region().united(clock_rect()));
        }

        void open_cv() noexcept {
            if (cv_open) {
                return;
            }
            // One flyout at a time, like the real shells.
            if (start_open) {
                deskbar_close_start();
            }
            if (qs_open) {
                deskbar_close_qs();
            }
            if (nc_open) {
                deskbar_close_nc();
            }
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            if (hype::win::wm_ctx_visible()) {
                hype::win::wm_ctx_close();
            }
            if (cal_open) {
                deskbar_close_cal();
            }
            cv_open = true;
            cv_age = 0;
            serial::write("clip hist=");
            serial::write_dec32(clip_count());
            serial::write("\r\n");
            invalidate(cv_anim_region());
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

        bool searching() noexcept { return search_len != 0; }

        // Case-insensitive substring match of the query against `name`
        // (an empty query passes everything through).
        bool query_match(const char* name) noexcept {
            if (search_len == 0) {
                return true;
            }
            for (hcpp::u32 i = 0; name[i] != '\0'; ++i) {
                hcpp::u32 j = 0;
                while (j < search_len && name[i + j] != '\0' &&
                       upper(name[i + j]) == upper(search_query[j])) {
                    ++j;
                }
                if (j == search_len) {
                    return true;
                }
            }
            return false;
        }

        void build_menu_order() noexcept {
            const hcpp::u32 n = hype::hxrt::hxrt_count();
            const hype::hxrt::HxerHeader* const* list =
                hype::hxrt::hxrt_list();
            menu_count = 0;
            for (hcpp::u32 i = 0; i < n && menu_count < menu_max_items;
                 ++i) {
                if (list[i] == nullptr || !query_match(list[i]->name)) {
                    continue;
                }
                menu_order[menu_count++] = i;
            }
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

        void path_join(char* out, hcpp::size_t cap, const char* dir,
                       const char* leaf) noexcept {
            hcpp::size_t n = 0;
            while (n + 1 < cap && dir[n] != '\0') {
                out[n] = dir[n];
                ++n;
            }
            if (n > 0 && out[n - 1] != '/' && n + 1 < cap) {
                out[n++] = '/';
            }
            copy_text(out + n, cap - n, leaf);
        }

        void file_hit_add(const hype::vfs::FsStat& entry,
                          const char* dir) noexcept {
            if (file_hits_count >= search_files_max) {
                return;
            }
            FileHit& hit = file_hits[file_hits_count];
            copy_text(hit.name, sizeof(hit.name), entry.name);
            path_join(hit.path, sizeof(hit.path), dir, entry.name);
            hit.flags = entry.flags;
            ++file_hits_count;
        }

        // Bounded walk: U:/ root entries plus one level into each
        // directory that fits the scan budget (~128 entries max).
        void collect_file_hits() noexcept {
            file_hits_count = 0;
            if (search_len == 0) {
                return;
            }
            // Listing scratch stays off the stack: this runs inside the
            // keyboard IRQ path (emit_key_event -> wm -> deskbar).
            static hype::vfs::FsStat root[search_list_cap];
            static hype::vfs::FsStat kids[search_list_cap];
            const hcpp::u32 found =
                hype::vfs::fs_list(search_root, root, search_list_cap);
            const hcpp::u32 n =
                found < search_list_cap ? found : search_list_cap;
            hcpp::u32 scanned = 0;
            for (hcpp::u32 i = 0; i < n && scanned < search_scan_max; ++i) {
                ++scanned;
                if (query_match(root[i].name)) {
                    file_hit_add(root[i], search_root);
                }
                if ((root[i].flags & hype::vfs::FS_DIR) == 0 ||
                    file_hits_count >= search_files_max) {
                    continue;
                }
                char sub[hype::vfs::hypepath_max_length];
                path_join(sub, sizeof(sub), search_root, root[i].name);
                const hcpp::u32 kfound =
                    hype::vfs::fs_list(sub, kids, search_list_cap);
                const hcpp::u32 kn =
                    kfound < search_list_cap ? kfound : search_list_cap;
                for (hcpp::u32 j = 0; j < kn && scanned < search_scan_max;
                     ++j) {
                    ++scanned;
                    if (query_match(kids[j].name)) {
                        file_hit_add(kids[j], sub);
                    }
                }
            }
        }

        // Clickable result rows: filtered apps first, then file hits
        // (files only exist while a query is active).
        hcpp::u32 menu_result_rows() noexcept {
            return menu_count + (searching() ? file_hits_count : 0);
        }

        Rect compute_menu() noexcept {
            if (!start_open) {
                return {};
            }
            hcpp::i32 h = menu_pad * 2 + menu_search_h + menu_search_gap;
            if (!searching()) {
                const hcpp::u32 rows = menu_count > 0 ? menu_count : 1;
                h += menu_head_h +
                     static_cast<hcpp::i32>(rows) * menu_item_h;
            } else {
                if (menu_count > 0) {
                    h += menu_section_h +
                         static_cast<hcpp::i32>(menu_count) * menu_item_h;
                }
                if (file_hits_count > 0) {
                    h += menu_section_h +
                         static_cast<hcpp::i32>(file_hits_count) *
                             menu_item_h;
                }
                if (menu_result_rows() == 0) {
                    h += menu_item_h;  // "No results" row
                }
            }
            Rect r{bar.x + bar_pad, bar.y - 4 - h, menu_w, h};
            if (r.y < 2) {
                r.y = 2;
            }
            return r;
        }

        Rect menu_field_rect(const Rect& menu) noexcept {
            return {menu.x + menu_pad, menu.y + menu_pad,
                    menu.w - 2 * menu_pad, menu_search_h};
        }

        // Screen rect of combined result row `index` (apps first, file
        // hits after). Kept in sync with paint_menu's section headers:
        // each section label sits exactly menu_section_h above its
        // section's first row.
        Rect menu_row_rect(const Rect& menu, hcpp::u32 index) noexcept {
            hcpp::i32 y = menu.y + menu_pad + menu_search_h +
                          menu_search_gap;
            if (!searching()) {
                return {menu.x + 1,
                        y + menu_head_h +
                            static_cast<hcpp::i32>(index) * menu_item_h,
                        menu.w - 2, menu_item_h};
            }
            if (index < menu_count) {
                return {menu.x + 1,
                        y + menu_section_h +
                            static_cast<hcpp::i32>(index) * menu_item_h,
                        menu.w - 2, menu_item_h};
            }
            if (menu_count > 0) {
                y += menu_section_h +
                     static_cast<hcpp::i32>(menu_count) * menu_item_h;
            }
            y += menu_section_h;
            return {menu.x + 1,
                    y + static_cast<hcpp::i32>(index - menu_count) *
                            menu_item_h,
                    menu.w - 2, menu_item_h};
        }

        hcpp::i32 menu_row_at(Point p) noexcept {
            const Rect r = compute_menu();
            if (r.empty() || !r.contains(p)) {
                return -1;
            }
            const hcpp::u32 rows = menu_result_rows();
            for (hcpp::u32 i = 0; i < rows; ++i) {
                if (menu_row_rect(r, i).contains(p)) {
                    return static_cast<hcpp::i32>(i);
                }
            }
            return -1;
        }

        // Rebuilds filtered results after the query text changed.
        // `before` is the menu paint bounds captured *before* the query
        // mutated, so the union always covers both geometries.
        void search_refresh(const Rect& before) noexcept {
            build_menu_order();
            collect_file_hits();
            if (search_sel >= menu_result_rows()) {
                search_sel = 0;
            }
            serial::write("start search=");
            serial::write(search_query);
            serial::write(" hits=");
            serial::write_dec32(menu_result_rows());
            serial::write("\r\n");
            invalidate(
                before.united(rui_paint_bounds(compute_menu())));
        }

        void search_push(char c) noexcept {
            if (search_len >= search_cap) {
                return;
            }
            const Rect before = rui_paint_bounds(compute_menu());
            search_query[search_len++] = c;
            search_query[search_len] = '\0';
            search_refresh(before);
        }

        void search_pop() noexcept {
            if (search_len == 0) {
                return;
            }
            const Rect before = rui_paint_bounds(compute_menu());
            search_query[--search_len] = '\0';
            search_refresh(before);
        }

        void search_clear() noexcept {
            if (search_len == 0) {
                return;
            }
            const Rect before = rui_paint_bounds(compute_menu());
            search_len = 0;
            search_query[0] = '\0';
            search_refresh(before);
        }

        // Activates a combined result row: apps launch through the
        // normal desk_open_app path, file hits open HxFiles on the
        // containing dir (dir hits navigate straight into it).
        void launch_result(hcpp::u32 index) noexcept {
            if (index < menu_count) {
                const hype::hxrt::HxerHeader* const* list =
                    hype::hxrt::hxrt_list();
                const hype::hxrt::HxerHeader* app =
                    list[menu_order[index]];
                if (app != nullptr) {
                    serial::write("start launch=");
                    serial::write_line(app->name);
                    hype::win::desk_open_app(app->name);
                }
                return;
            }
            const hcpp::u32 f = index - menu_count;
            if (!searching() || f >= file_hits_count) {
                return;
            }
            const FileHit& hit = file_hits[f];
            serial::write("start launch=");
            serial::write_line(hit.name);
            char target[hype::vfs::hypepath_max_length];
            copy_text(target, sizeof(target), hit.path);
            if ((hit.flags & hype::vfs::FS_DIR) == 0) {
                // Files: navigate to the parent dir — trim the leaf
                // component (a root-level file leaves "HYPEOS://U").
                hcpp::u32 last = 0;
                for (hcpp::u32 i = 0; target[i] != '\0'; ++i) {
                    if (target[i] == '/') {
                        last = i;
                    }
                }
                if (last > 9) {
                    target[last] = '\0';
                }
            }
            hype::hxrt::hxfiles_nav(target);
            hype::win::desk_open_app("HxFiles");
        }

        void open_start() noexcept {
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            if (qs_open) {
                deskbar_close_qs();
            }
            if (nc_open) {
                deskbar_close_nc();
            }
            // Fresh query every open — the field is empty and the full
            // app list shows until the first keystroke filters it.
            search_len = 0;
            search_query[0] = '\0';
            file_hits_count = 0;
            search_sel = 0;
            if (cv_open) {
                deskbar_close_clip();
            }
            if (cal_open) {
                deskbar_close_cal();
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
            if (nc_open) {
                deskbar_close_nc();
            }
            if (cv_open) {
                deskbar_close_clip();
            }
            if (cal_open) {
                deskbar_close_cal();
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

        void open_nc() noexcept {
            if (nc_open) {
                return;
            }
            // One flyout at a time, like the real shells.
            if (start_open) {
                deskbar_close_start();
            }
            if (qs_open) {
                deskbar_close_qs();
            }
            if (hype::win::wm_menu_visible()) {
                hype::win::wm_menu_close();
            }
            if (hype::win::wm_ctx_visible()) {
                hype::win::wm_ctx_close();
            }
            if (cv_open) {
                deskbar_close_clip();
            }
            if (cal_open) {
                deskbar_close_cal();
            }
            nc_open = true;
            nc_age = 0;
            serial::write_line("notif center=open");
            // Opening the archive marks everything read.
            if (unread != 0) {
                unread = 0;
                serial::write_line("tray unread=0");
            }
            invalidate(nc_anim_region().united(bell_zone()));
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
            // Persisted pref (issue #130): survives reboot via hype.cfg.
            hype::cfg::settings_set_int("audio.volume", pct);
            hype::cfg::settings_request_save();
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
            // Persisted pref (issue #130): survives reboot via hype.cfg.
            hype::cfg::settings_set_int("ui.brightness", pct);
            hype::cfg::settings_request_save();
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

        // Numbered desktop squares (issue #135): the current desktop
        // gets the accent frame + bright digit, the rest stay muted.
        void paint_vdesk_strip(Canvas& canvas, const Theme& theme) noexcept {
            const hcpp::u32 n = hype::win::vdesk_count();
            const hcpp::u32 cur = hype::win::vdesk_current();
            for (hcpp::u32 i = 0; i < n; ++i) {
                const Rect cell = vdesk_cell(i);
                const bool on = i == cur;
                rui_control_bg(
                    canvas, cell,
                    on ? rui_lift(theme.secondary, 2) : theme.secondary,
                    on ? theme.accent : theme.border);
                char digit[2] = {static_cast<char>('1' + i), '\0'};
                paint_label(canvas, cell.x + (cell.w - 6) / 2,
                            label_top(cell), hcpp::StringView{digit, 1},
                            on ? theme.card_fg : theme.muted_fg, 11);
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

        void paint_search_field(Canvas& canvas, const Theme& theme,
                                const Rect& field) noexcept {
            rui_control_bg(canvas, field, theme.secondary, theme.accent);
            const hcpp::u32 muted = canvas.color(theme.muted_fg);
            const hcpp::i32 cy = field.y + field.h / 2;
            // Magnifier: stroked lens + short handle.
            canvas.stroke_circle(field.x + 11, cy - 1, 4, muted);
            canvas.draw_line(field.x + 14, cy + 2, field.x + 17, cy + 5,
                             muted);
            const hcpp::i32 tx = field.x + 24;
            if (search_len == 0) {
                paint_label(canvas, tx, label_top(field), "Search",
                            theme.muted_fg, 13);
                return;
            }
            const hcpp::StringView text{search_query, search_len};
            // The field is always focused while the menu is open — clip
            // so an over-long query never bleeds past the control edge.
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(
                Rect{tx, field.y, field.right() - tx - 4, field.h}));
            paint_label(canvas, tx, label_top(field), text,
                        theme.card_fg, 13);
            canvas.set_clip(saved_clip);
            // Caret right after the measured text.
            const hcpp::i32 tw =
                hype::font::hypefont_ready()
                    ? hype::font::text_measure(text, 13)
                    : static_cast<hcpp::i32>(search_len) * 8;
            hcpp::i32 cx = tx + tw + 1;
            if (cx > field.right() - 4) {
                cx = field.right() - 4;
            }
            canvas.fill_rect(Rect{cx, cy - 6, 1, 12},
                             canvas.color(theme.accent));
        }

        void paint_section_label(Canvas& canvas, const Theme& theme,
                                 const Rect& menu, hcpp::i32 y,
                                 const char* text) noexcept {
            paint_label(canvas, menu.x + menu_pad + 6, y + 3, text,
                        theme.muted_fg, 11);
        }

        void paint_file_glyph(Canvas& canvas, const Theme& theme,
                              const Rect& row, bool is_dir) noexcept {
            const hcpp::i32 cy = row.y + row.h / 2;
            const hcpp::i32 gx = row.x + 9;
            if (is_dir) {
                const hcpp::u32 folder = canvas.color(theme.accent);
                canvas.fill_rect(Rect{gx, cy - 5, 5, 3}, folder);
                canvas.fill_rect(Rect{gx, cy - 3, 12, 9}, folder);
                canvas.fill_rect(Rect{gx, cy - 3, 12, 1},
                                 canvas.color(rui_lift(theme.accent, 1)));
                return;
            }
            // Mini page glyph: card sheet + folded corner + text lines.
            const hcpp::u32 page = canvas.color(theme.card);
            canvas.fill_rect(Rect{gx + 1, cy - 6, 10, 13}, page);
            canvas.stroke_rect(Rect{gx + 1, cy - 6, 10, 13},
                               canvas.color(theme.border));
            const hcpp::u32 edge = canvas.color(theme.border);
            canvas.draw_line(gx + 8, cy - 6, gx + 11, cy - 3, edge);
            const hcpp::u32 ink = canvas.color(theme.muted_fg);
            canvas.fill_rect(Rect{gx + 3, cy - 1, 6, 1}, ink);
            canvas.fill_rect(Rect{gx + 3, cy + 2, 6, 1}, ink);
        }

        void paint_menu(Canvas& canvas, const Theme& theme,
                        const Rect& menu) noexcept {
            rui_panel(canvas, menu, rui_elev_panel);
            paint_search_field(canvas, theme, menu_field_rect(menu));
            const hcpp::u32 card_lift =
                rui_lift(theme.card, rui_elev_panel);
            if (!searching()) {
                const Rect head{menu.x + 1,
                                menu.y + menu_pad + menu_search_h +
                                    menu_search_gap,
                                menu.w - 2, menu_head_h};
                canvas.fill_rect(head, canvas.color(card_lift));
                canvas.fill_circle(head.x + 12, head.y + head.h / 2, 4,
                                   canvas.color(theme.accent));
                paint_label(canvas, head.x + 24, label_top(head),
                            "HypeAI OS", theme.muted_fg, 13);
            } else if (menu_result_rows() == 0) {
                const Rect row{menu.x + 1,
                               menu.y + menu_pad + menu_search_h +
                                   menu_search_gap,
                               menu.w - 2, menu_item_h};
                canvas.fill_rect(row, canvas.color(card_lift));
                paint_label(canvas, row.x + 10, label_top(row),
                            "No results", theme.muted_fg, 13);
            }
            const hype::hxrt::HxerHeader* const* list =
                hype::hxrt::hxrt_list();
            const hcpp::u32 rows =
                !searching() ? (menu_count > 0 ? menu_count : 1)
                             : menu_result_rows();
            if (searching() && menu_count > 0) {
                paint_section_label(canvas, theme, menu,
                                    menu_row_rect(menu, 0).y -
                                        menu_section_h,
                                    "Apps");
            }
            if (searching() && file_hits_count > 0) {
                paint_section_label(canvas, theme, menu,
                                    menu_row_rect(menu, menu_count).y -
                                        menu_section_h,
                                    "Files");
            }
            for (hcpp::u32 i = 0; i < rows; ++i) {
                const Rect row = menu_row_rect(menu, i);
                canvas.fill_rect(row, canvas.color(card_lift));
                const bool sel = i == search_sel;
                if (sel) {
                    canvas.fill_rect_alpha(row, canvas.color(theme.accent),
                                           48);
                    canvas.fill_rect(Rect{row.x, row.y, 2, row.h},
                                     canvas.color(theme.accent));
                }
                const hcpp::i32 cy = row.y + row.h / 2;
                if (menu_count == 0 && !searching()) {
                    paint_label(canvas, row.x + 10, label_top(row),
                                "No apps", theme.muted_fg, 13);
                    continue;
                }
                const Rect saved_clip = canvas.clip();
                canvas.set_clip(saved_clip.intersection(row));
                if (i < menu_count) {
                    const hype::hxrt::HxerHeader* app =
                        list[menu_order[i]];
                    const Rect icon{row.x + 8, cy - 7, 14, 14};
                    rui_rounded_gradient(
                        canvas, icon, 4, rui_lift(theme.primary, 1),
                        theme.primary);
                    char mono[2] = {upper(app->name[0]), '\0'};
                    paint_label(canvas, icon.x + 4, icon.y + 1,
                                hcpp::StringView{mono, 1},
                                theme.primary_fg, 11);
                    paint_label(canvas, row.x + 30, label_top(row),
                                hcpp::StringView{app->name},
                                theme.card_fg, 13);
                } else {
                    const FileHit& hit = file_hits[i - menu_count];
                    const bool is_dir =
                        (hit.flags & hype::vfs::FS_DIR) != 0;
                    paint_file_glyph(canvas, theme, row, is_dir);
                    paint_label(canvas, row.x + 30, label_top(row),
                                hcpp::StringView{hit.name},
                                theme.card_fg, 13);
                }
                canvas.set_clip(saved_clip);
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

            // Lock / Power buttons: real session calls (lock veil /
            // shutdown-restart dialog) with serial markers.
            const Rect lock = qs_lock_btn(panel);
            const Rect power = qs_power_btn(panel);
            rui_control_bg(canvas, lock, theme.secondary, theme.border);
            paint_label(canvas, lock.x + (lock.w - 24) / 2, label_top(lock),
                        "Lock", theme.card_fg, 13);
            rui_control_bg(canvas, power, theme.secondary,
                           theme.destructive);
            paint_label(canvas, power.x + (power.w - 36) / 2,
                        label_top(power), "Power", theme.card_fg, 13);

        }

        // One history row: kind-tinted icon, title + clipped message,
        // "HH:MM" wall-clock stamp and a top-right "x" dismiss hitbox.
        void paint_nc_row(Canvas& canvas, const Theme& theme,
                          const Rect& row, const HistEntry& e) noexcept {
            canvas.fill_rect(
                row, canvas.color(rui_lift(theme.card, rui_elev_panel)));
            const Rect icon{row.x + 8, row.y + 7, 14, 14};
            const hcpp::u32 krgb = nc_kind_rgb(e.kind, theme);
            rui_rounded_gradient(canvas, icon, 4, rui_lift(krgb, 1), krgb);
            char mono[2] = {upper(e.title[0]), '\0'};
            paint_label(canvas, icon.x + 4, icon.y + 1,
                        hcpp::StringView{mono, 1}, theme.primary_fg, 11);

            const Rect xbox = nc_row_x(row);
            char clk[6]{"--:--"};
            if (e.hour != 0xFF) {
                clk[0] = static_cast<char>('0' + e.hour / 10);
                clk[1] = static_cast<char>('0' + e.hour % 10);
                clk[2] = ':';
                clk[3] = static_cast<char>('0' + e.minute / 10);
                clk[4] = static_cast<char>('0' + e.minute % 10);
                clk[5] = '\0';
            }
            paint_label(canvas, xbox.x - 38, row.y + 5,
                        hcpp::StringView{clk, 5}, theme.muted_fg, 11);
            // "x" glyph drawn as strokes so it reads at 11px.
            const hcpp::u32 xrgb = canvas.color(theme.muted_fg);
            canvas.draw_line(xbox.x + 4, xbox.y + 4, xbox.x + 11,
                             xbox.y + 11, xrgb);
            canvas.draw_line(xbox.x + 11, xbox.y + 4, xbox.x + 4,
                             xbox.y + 11, xrgb);

            const Rect saved_clip = canvas.clip();
            const Rect title_clip{row.x + 28, row.y,
                                  xbox.x - 44 - (row.x + 28), 20};
            canvas.set_clip(saved_clip.intersection(title_clip));
            paint_label(canvas, row.x + 30, row.y + 5,
                        hcpp::StringView{e.title}, theme.card_fg, 13);
            const Rect msg_clip{row.x + 28, row.y + 20,
                                row.w - 36, row.h - 20};
            canvas.set_clip(saved_clip.intersection(msg_clip));
            paint_label(canvas, row.x + 30, row.y + 24,
                        hcpp::StringView{e.msg}, theme.muted_fg, 12);
            canvas.set_clip(saved_clip);
        }

        void paint_nc(Canvas& canvas, const Theme& theme,
                      const Rect& panel) noexcept {
            rui_panel(canvas, panel, rui_elev_panel);
            const Rect head{panel.x + nc_pad, panel.y + nc_pad,
                            panel.w - 2 * nc_pad, nc_head_h};
            canvas.fill_circle(head.x + 4, head.y + head.h / 2, 4,
                               canvas.color(theme.accent));
            paint_label(canvas, head.x + 16, label_top(head),
                        "Notifications", theme.card_fg, 13);
            const Rect clear = nc_clear_btn(panel);
            rui_control_bg(canvas, clear, theme.secondary, theme.border);
            paint_label(canvas, clear.x + (clear.w - 28) / 2,
                        label_top(clear), "Clear", theme.card_fg, 13);
            canvas.fill_rect(
                Rect{panel.x + nc_pad, head.bottom() + 2,
                     panel.w - 2 * nc_pad, 1},
                canvas.color(theme.border));

            const hcpp::u32 rows = nc_rows_fit(panel);
            if (rows == 0) {
                const Rect list = nc_list(panel);
                paint_label(canvas, list.x + 10, list.y + 12,
                            "No notifications", theme.muted_fg, 13);
            }
            for (hcpp::u32 i = 0; i < rows; ++i) {
                paint_nc_row(canvas, theme, nc_row_rect(panel, i),
                             hist[hist_slot(i)]);
            }
        }

        // One clipboard row: number-key badge + flattened text preview.
        void paint_cv_row(Canvas& canvas, const Theme& theme,
                          const Rect& row, hcpp::u32 index) noexcept {
            canvas.fill_rect(
                row, canvas.color(rui_lift(theme.card, rui_elev_panel)));
            hcpp::i32 text_x = row.x + 10;
            if (index < 9) {
                const Rect badge{row.x + 4,
                                 row.y + (row.h - 16) / 2, 16, 16};
                rui_control_bg(canvas, badge, theme.secondary,
                               theme.border);
                char digit[2] = {static_cast<char>('1' + index), '\0'};
                paint_label(canvas, badge.x + 5, badge.y + 2,
                            hcpp::StringView{digit, 1}, theme.muted_fg,
                            11);
                text_x = row.x + 26;
            }
            // Preview = first ~40 chars, control bytes flattened to
            // spaces so newlines never smear a second visual line.
            char preview[cv_preview_max + 1];
            hcpp::u32 len = 0;
            const char* text = clip_history(index, len);
            hcpp::u32 n = 0;
            if (text != nullptr) {
                while (n < cv_preview_max && n < len) {
                    const char c = text[n];
                    preview[n] = (c >= 32 && c < 127) ? c : ' ';
                    ++n;
                }
            }
            preview[n] = '\0';
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(
                Rect{text_x, row.y, row.right() - 6 - text_x, row.h}));
            paint_label(canvas, text_x, label_top(row),
                        hcpp::StringView{preview, n}, theme.card_fg, 13);
            canvas.set_clip(saved_clip);
        }

        void paint_cv(Canvas& canvas, const Theme& theme,
                      const Rect& panel) noexcept {
            rui_panel(canvas, panel, rui_elev_panel);
            const Rect head{panel.x + cv_pad, panel.y + cv_pad,
                            panel.w - 2 * cv_pad, cv_head_h};
            canvas.fill_circle(head.x + 4, head.y + head.h / 2, 4,
                               canvas.color(theme.accent));
            paint_label(canvas, head.x + 16, label_top(head), "Clipboard",
                        theme.card_fg, 13);
            paint_label(canvas, head.right() - 40, label_top(head),
                        "Win+V", theme.muted_fg, 11);
            canvas.fill_rect(
                Rect{panel.x + cv_pad, head.bottom() + 2,
                     panel.w - 2 * cv_pad, 1},
                canvas.color(theme.border));
            const hcpp::u32 rows = cv_rows();
            if (rows == 0) {
                const Rect row = cv_row_rect(panel, 0);
                paint_label(canvas, row.x + 10, label_top(row),
                            "Clipboard is empty", theme.muted_fg, 13);
            }
            for (hcpp::u32 i = 0; i < rows; ++i) {
                paint_cv_row(canvas, theme, cv_row_rect(panel, i), i);
            }
        }

        // Centers `text` horizontally in `row` (top-anchored paint) and
        // returns the x used — measurement matches paint_search_field:
        // hypefont metrics when ready, 8px cells otherwise.
        hcpp::i32 cal_text_center(const Rect& row, hcpp::StringView text,
                                  hcpp::u32 px) noexcept {
            const hcpp::i32 tw =
                hype::font::hypefont_ready()
                    ? hype::font::text_measure(text, px)
                    : static_cast<hcpp::i32>(text.size()) *
                          static_cast<hcpp::i32>(px) / 2;
            hcpp::i32 x = row.x + (row.w - tw) / 2;
            if (x < row.x) {
                x = row.x;
            }
            return x;
        }

        void paint_cal(Canvas& canvas, const Theme& theme,
                       const Rect& panel) noexcept {
            static const char* const month_names[12] = {
                "January",  "February", "March",    "April",
                "May",      "June",     "July",     "August",
                "September", "October", "November", "December"};
            static const char* const wd_full[7] = {
                "Monday", "Tuesday",  "Wednesday", "Thursday",
                "Friday", "Saturday", "Sunday"};
            static const char* const wd_short[7] = {"Mo", "Tu", "We",
                                                    "Th", "Fr", "Sa",
                                                    "Su"};
            rui_panel(canvas, panel, rui_elev_panel);
            hype::time::RtcTime now{};
            const bool have_rtc = hype::time::rtc_now(now);

            // Big clock: same HH:MM state the tray cell paints.
            char clk[6]{"--:--"};
            if (clock_hour != 0xFF) {
                clk[0] = static_cast<char>('0' + clock_hour / 10);
                clk[1] = static_cast<char>('0' + clock_hour % 10);
                clk[2] = ':';
                clk[3] = static_cast<char>('0' + clock_minute / 10);
                clk[4] = static_cast<char>('0' + clock_minute % 10);
                clk[5] = '\0';
            }
            const Rect clk_row = cal_clock_row(panel);
            paint_label(canvas,
                        cal_text_center(clk_row,
                                        hcpp::StringView{clk, 5}, 24),
                        clk_row.y + 2, hcpp::StringView{clk, 5},
                        theme.card_fg, 24);

            // Full-date line: "Wednesday, 21 January 2026".
            const Rect date_row = cal_date_row(panel);
            char dbuf[48];
            if (have_rtc) {
                hcpp::size_t o = 0;
                const char* wd = wd_full[cal_weekday(
                    now.year, now.month, now.day)];
                while (*wd != '\0') {
                    dbuf[o++] = *wd++;
                }
                dbuf[o++] = ',';
                dbuf[o++] = ' ';
                o += fmt_u32(dbuf + o, now.day);
                dbuf[o++] = ' ';
                const char* mn = month_names[(now.month - 1) % 12];
                while (*mn != '\0') {
                    dbuf[o++] = *mn++;
                }
                dbuf[o++] = ' ';
                o += fmt_u32(dbuf + o, now.year);
                dbuf[o] = '\0';
            } else {
                copy_text(dbuf, sizeof(dbuf), "no clock");
            }
            paint_label(canvas,
                        cal_text_center(date_row, hcpp::StringView{dbuf},
                                        13),
                        label_top(date_row), hcpp::StringView{dbuf},
                        theme.muted_fg, 13);

            canvas.fill_rect(
                Rect{panel.x + cal_pad, date_row.bottom() + 4,
                     panel.w - 2 * cal_pad, 1},
                canvas.color(theme.border));

            // Nav row: "<" prev, centered "Month YYYY", ">" next; the
            // arrow greys out once the ±120-month bound is reached.
            const Rect nav = cal_nav_row(panel);
            const Rect prev = cal_prev_btn(panel);
            const Rect next = cal_next_btn(panel);
            rui_control_bg(canvas, prev, theme.secondary, theme.border);
            rui_control_bg(canvas, next, theme.secondary, theme.border);
            paint_label(canvas, prev.x + (prev.w - 8) / 2,
                        label_top(prev), "<",
                        cal_nav > -cal_nav_max ? theme.card_fg
                                               : theme.muted_fg,
                        13);
            paint_label(canvas, next.x + (next.w - 8) / 2,
                        label_top(next), ">",
                        cal_nav < cal_nav_max ? theme.card_fg
                                              : theme.muted_fg,
                        13);
            char nbuf[24];
            hcpp::size_t o = 0;
            const char* mn = month_names[(cal_view_m - 1) % 12];
            while (*mn != '\0' && o + 1 < sizeof(nbuf)) {
                nbuf[o++] = *mn++;
            }
            nbuf[o++] = ' ';
            o += fmt_u32(nbuf + o,
                         static_cast<hcpp::u32>(cal_view_y));
            paint_label(canvas,
                        cal_text_center(nav, hcpp::StringView{nbuf}, 13),
                        label_top(nav), hcpp::StringView{nbuf},
                        theme.card_fg, 13);

            // Weekday header, Monday first (Mo..Su).
            const Rect wdrow = cal_wd_row(panel);
            const hcpp::i32 cw = wdrow.w / 7;
            for (hcpp::u32 c = 0; c < 7; ++c) {
                const Rect col{wdrow.x + static_cast<hcpp::i32>(c) * cw,
                               wdrow.y, cw, wdrow.h};
                paint_label(canvas,
                            cal_text_center(
                                col, hcpp::StringView{wd_short[c]}, 11),
                            wdrow.y + 2, hcpp::StringView{wd_short[c]},
                            theme.muted_fg, 11);
            }

            // Month grid: leading cells before the 1st stay empty; only
            // the viewed month's days are drawn (no trailing grey days).
            const hcpp::u32 col0 =
                cal_weekday(cal_view_y, cal_view_m, 1);
            const hcpp::i32 ndays =
                cal_month_days(cal_view_y, cal_view_m);
            for (hcpp::u32 i = 0; i < 42; ++i) {
                const hcpp::i32 day =
                    static_cast<hcpp::i32>(i) -
                    static_cast<hcpp::i32>(col0) + 1;
                if (day < 1 || day > ndays) {
                    continue;
                }
                const Rect cell =
                    cal_cell(panel, i / 7, i % 7);
                char dnum[3];
                dnum[0] = static_cast<char>('0' + day / 10);
                dnum[1] = static_cast<char>('0' + day % 10);
                const hcpp::u32 dlen = day > 9 ? 2 : 1;
                const hcpp::StringView dtext{
                    dnum + (day > 9 ? 0 : 1), dlen};
                const bool today =
                    have_rtc && now.year == cal_view_y &&
                    now.month == cal_view_m && now.day == day;
                if (today) {
                    canvas.fill_circle(cell.x + cell.w / 2,
                                       cell.y + cell.h / 2, 11,
                                       canvas.color(theme.accent));
                }
                paint_label(canvas, cal_text_center(cell, dtext, 13),
                            label_top(cell), dtext,
                            today ? theme.primary_fg : theme.card_fg,
                            13);
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
        // Watchdog heartbeat (issue #140): this is the shell's per-pass
        // UI pump — while the desktop loop is live it runs every idle
        // iteration, so a UI freeze starves the "ui" source and trips.
        wdt::wdt_pet_name("ui");
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
        if (nc_open && nc_age < nc_anim_ticks) {
            ++nc_age;
            invalidate(nc_anim_region());
        }
        if (cv_open && cv_age < cv_anim_ticks) {
            ++cv_age;
            invalidate(cv_anim_region());
        }
        if (cal_open && cal_age < cal_anim_ticks) {
            ++cal_age;
            invalidate(cal_anim_region());
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
                    if (cal_open) {
                        // The flyout's big clock rides the same minute.
                        invalidate(rui_paint_bounds(cal_rect_now()));
                    }
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
        const Rect nc = nc_rect_now();
        const Rect cv = cv_rect_now();
        const Rect cal = cal_rect_now();
        const Rect area = bar.intersection(clip);
        const Rect menu_area = menu.intersection(clip);
        const Rect qs_area = qs.intersection(clip);
        const Rect nc_area = nc.intersection(clip);
        const Rect cv_area = cv.intersection(clip);
        const Rect cal_area = cal.intersection(clip);
        const Rect toast_area = toasts.count() > 0
                                    ? toast_region().intersection(clip)
                                    : Rect{};
        if (area.empty() && menu_area.empty() && qs_area.empty() &&
            nc_area.empty() && cv_area.empty() && cal_area.empty() &&
            toast_area.empty()) {
            return;
        }
        Canvas canvas(fb);
        canvas.set_clip(clip);
        const Theme& theme = theme_active();
        if (!area.empty()) {
            // HypeUI taskbar: flat elevated surface, 1px hairline top
            // border, and a soft upward shadow (shadow-l direction up)
            // fading into the desktop above.
            for (hcpp::i32 i = 0; i < 8; ++i) {
                canvas.fill_rect_alpha(
                    Rect{bar.x, bar.y - 1 - i, bar.w, 1},
                    canvas.color(0x000000),
                    static_cast<hcpp::u8>((8 - i) * 3));
            }
            canvas.fill_rect(bar,
                             canvas.color(hype::ui::hui::colors().surface_elev));
            canvas.fill_rect(Rect{bar.x, bar.y, bar.w, 1},
                             canvas.color(theme.border));
            paint_start_button(canvas, theme);
            // Task buttons stop at the desktop strip, which stops at the
            // tray (issue #135). wm_window_at only enumerates the current
            // desktop's windows, so the buttons are per-desktop already.
            const hcpp::i32 limit = vdesk_strip().x - task_gap;
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
            paint_vdesk_strip(canvas, theme);
            paint_tray(canvas, theme);
        }
        if (!menu_area.empty()) {
            paint_menu(canvas, theme, menu);
        }
        if (!qs_area.empty()) {
            paint_qs(canvas, theme, qs);
        }
        if (!nc_area.empty()) {
            paint_nc(canvas, theme, nc);
        }
        if (!cv_area.empty()) {
            paint_cv(canvas, theme, cv);
        }
        if (!cal_area.empty()) {
            paint_cal(canvas, theme, cal);
        }
        // Toasts sit in the band above the bar, topmost of the shell UI.
        if (toasts.count() > 0 && !toast_region().empty()) {
            const Rect band{screen.x, screen.y, screen.w, bar.y - 2};
            toasts.paint(canvas, band);
        }
        if (!painted_once) {
            painted_once = true;
            serial::write_line("deskbar ok");
            serial::write_line("hui restyle=ok");
        }
    }

    bool deskbar_handle_mouse(hcpp::i32 x, hcpp::i32 y,
                              bool pressed) noexcept {
        if (!pressed) {
            return false;
        }
        const Point p{x, y};
        if (start_open) {
            const Rect menu = compute_menu();
            // Clicks inside the search field keep the menu open (it is
            // always focused); result rows launch, everything else
            // dismisses.
            if (menu.contains(p) && menu_field_rect(menu).contains(p)) {
                return true;
            }
            const hcpp::i32 row = menu_row_at(p);
            if (row >= 0) {
                launch_result(static_cast<hcpp::u32>(row));
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
                    serial::write_line("qs lock=real");
                    deskbar_close_qs();
                    hype::session::session_lock();
                    return true;
                }
                if (qs_power_btn(panel).contains(p)) {
                    serial::write_line("qs power=real");
                    deskbar_close_qs();
                    hype::session::session_power_dialog();
                    return true;
                }
                return true;
            }
            // Click outside dismisses the flyout, then the same click is
            // free to hit the bar/tray zones below.
            deskbar_close_qs();
        }
        const bool was_nc = nc_open;
        if (nc_open) {
            const Rect panel = nc_rect_now();
            if (panel.contains(p)) {
                if (nc_clear_btn(panel).contains(p)) {
                    hist_count = 0;
                    serial::write_line("notif hist=0");
                    invalidate(rui_paint_bounds(panel));
                    return true;
                }
                const hcpp::u32 rows = nc_rows_fit(panel);
                for (hcpp::u32 i = 0; i < rows; ++i) {
                    if (!nc_row_x(nc_row_rect(panel, i)).contains(p)) {
                        continue;
                    }
                    hist_remove(i);
                    serial::write("notif hist=");
                    serial::write_dec32(hist_count);
                    serial::write("\r\n");
                    invalidate(rui_paint_bounds(panel));
                    return true;
                }
                return true;
            }
            // Click outside dismisses, then the click may hit the bar.
            deskbar_close_nc();
        }
        if (cv_open) {
            const Rect panel = cv_rect_now();
            if (panel.contains(p)) {
                // Row click promotes that history entry and pastes it
                // into the still-focused window.
                const hcpp::u32 rows = cv_rows();
                for (hcpp::u32 i = 0; i < rows; ++i) {
                    if (!cv_row_rect(panel, i).contains(p)) {
                        continue;
                    }
                    clip_paste_at(i);
                    deskbar_close_clip();
                    return true;
                }
                return true;
            }
            // Click outside dismisses, then the click may hit the bar.
            deskbar_close_clip();
        }
        const bool was_cal = cal_open;
        if (cal_open) {
            const Rect panel = cal_rect_now();
            if (panel.contains(p)) {
                // Only the nav arrows are clickable inside the panel;
                // day cells are informational.
                if (cal_prev_btn(panel).contains(p)) {
                    cal_nav_month(-1);
                    return true;
                }
                if (cal_next_btn(panel).contains(p)) {
                    cal_nav_month(1);
                    return true;
                }
                return true;
            }
            // Click outside dismisses, then the click may hit the bar.
            deskbar_close_cal();
        }
        if (bar.empty() || !bar.contains(p)) {
            return false;
        }
        if (start_btn.contains(p)) {
            open_start();
            return true;
        }
        // Desktop strip (issue #135): a click on a numbered square
        // switches straight to that desktop.
        {
            const hcpp::u32 vdn = hype::win::vdesk_count();
            for (hcpp::u32 i = 0; i < vdn; ++i) {
                if (vdesk_cell(i).contains(p)) {
                    hype::win::vdesk_switch(i);
                    return true;
                }
            }
        }
        if (tray_rect().contains(p)) {
            if (speaker_zone().contains(p) || net_zone().contains(p)) {
                if (!was_qs) {
                    open_qs();
                }
                return true;
            }
            if (bell_zone().contains(p)) {
                // Bell toggles the notification-center flyout; opening it
                // also marks the archive read (unread -> 0).
                if (!was_nc) {
                    open_nc();
                }
                return true;
            }
            if (clock_rect().contains(p)) {
                // Clock cell toggles the calendar + clock flyout.
                if (!was_cal) {
                    open_cal();
                }
                return true;
            }
            // Remaining tray padding: consume without action.
            return true;
        }
        const hcpp::i32 limit = vdesk_strip().x - task_gap;
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
        // Archive a copy in the persistent history ring (overwrites the
        // oldest entry once full); the toast card stays transient.
        HistEntry& e = hist[hist_head];
        hist_head = (hist_head + 1) % hist_max;
        if (hist_count < hist_max) {
            ++hist_count;
        }
        copy_text(e.title, sizeof(e.title), title);
        copy_text(e.msg, sizeof(e.msg), msg);
        e.timestamp_ms = hype::time::uptime_ms();
        e.kind = ToastKind::Info;
        e.hour = 0xFF;
        e.minute = 0xFF;
        hype::time::RtcTime stamp{};
        if (hype::time::rtc_now(stamp)) {
            e.hour = stamp.hour;
            e.minute = stamp.minute;
        }
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
        serial::write("notif hist=");
        serial::write_dec32(hist_count);
        serial::write("\r\n");
        Rect dirty = toast_region().united(bell_zone());
        if (nc_open) {
            // A push while the archive is open must repaint the new row.
            dirty = dirty.united(rui_paint_bounds(nc_rect_now()));
        }
        invalidate(dirty);
    }

    bool deskbar_qs_open() noexcept { return qs_open; }

    void deskbar_set_volume(hcpp::i32 pct) noexcept {
        pct = clamp_pct(pct);
        if (pct == qs_vol) {
            return;
        }
        qs_vol = pct;
        if (qs_open) {
            invalidate(qs_vol_row(qs_rect_now()));
        }
    }

    void deskbar_set_brightness(hcpp::i32 pct) noexcept {
        pct = clamp_pct(pct);
        if (pct == qs_bright) {
            return;
        }
        qs_bright = pct;
        if (qs_open) {
            invalidate(qs_bright_row(qs_rect_now()));
        }
    }

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

    bool deskbar_nc_open() noexcept { return nc_open; }

    void deskbar_close_nc() noexcept {
        if (!nc_open) {
            return;
        }
        // Include the panel shadow spill so dismiss leaves no shadow ring.
        const Rect gone = rui_paint_bounds(nc_rect_now());
        nc_open = false;
        serial::write_line("notif center=close");
        invalidate(gone.united(bell_zone()));
    }

    // Right-click inside the open notification center dismisses the row
    // under the cursor (same result as its "x" hitbox).
    bool deskbar_handle_mouse_r(hcpp::i32 x, hcpp::i32 y) noexcept {
        if (cal_open) {
            // Same rule as the clipboard flyout: inside is consumed,
            // outside closes and falls through to right-click routing.
            const Point p{x, y};
            if (cal_rect_now().contains(p)) {
                return true;
            }
            deskbar_close_cal();
            return false;
        }
        if (cv_open) {
            // No right-click affordance in the clipboard flyout: inside
            // is consumed, outside closes and falls through to routing.
            const Point p{x, y};
            if (cv_rect_now().contains(p)) {
                return true;
            }
            deskbar_close_clip();
            return false;
        }
        if (!nc_open) {
            return false;
        }
        const Point p{x, y};
        const Rect panel = nc_rect_now();
        if (!panel.contains(p)) {
            deskbar_close_nc();
            return false;
        }
        const hcpp::u32 rows = nc_rows_fit(panel);
        for (hcpp::u32 i = 0; i < rows; ++i) {
            if (!nc_row_rect(panel, i).contains(p)) {
                continue;
            }
            hist_remove(i);
            serial::write("notif hist=");
            serial::write_dec32(hist_count);
            serial::write("\r\n");
            invalidate(rui_paint_bounds(panel));
            return true;
        }
        return true;
    }

    bool deskbar_clip_open() noexcept { return cv_open; }

    void deskbar_open_clip() noexcept {
        if (!cv_open) {
            open_cv();
        }
    }

    void deskbar_close_clip() noexcept {
        if (!cv_open) {
            return;
        }
        // Include the panel shadow spill so dismiss leaves no ring.
        const Rect gone = rui_paint_bounds(cv_rect_now());
        cv_open = false;
        serial::write_line("clip flyout=close");
        invalidate(gone);
    }

    void deskbar_toggle_clip() noexcept {
        if (cv_open) {
            deskbar_close_clip();
        } else {
            open_cv();
        }
    }

    bool deskbar_clip_key(hcpp::u16 code) noexcept {
        if (!cv_open) {
            return false;
        }
        if (code == 1) {  // escape
            deskbar_close_clip();
            return true;
        }
        // Number row: PS/2 codes 2..10 are '1'..'9' -> rows 0..8.
        if (code >= 2 && code <= 10) {
            clip_paste_at(code - 2);
            deskbar_close_clip();
            return true;
        }
        return false;
    }

    bool deskbar_cal_open() noexcept { return cal_open; }

    void deskbar_close_cal() noexcept {
        if (!cal_open) {
            return;
        }
        // Include the panel shadow spill so dismiss leaves no ring.
        const Rect gone = rui_paint_bounds(cal_rect_now());
        cal_open = false;
        serial::write_line("cal open=close");
        invalidate(gone.united(clock_rect()));
    }

    // Key hook while the calendar flyout is open: Esc closes, Left/Right
    // step the viewed month (same bound as the on-panel arrows).
    bool deskbar_cal_key(hcpp::u16 code) noexcept {
        if (!cal_open) {
            return false;
        }
        if (code == key_escape) {
            deskbar_close_cal();
            return true;
        }
        if (code == key_left) {
            cal_nav_month(-1);
            return true;
        }
        if (code == key_right) {
            cal_nav_month(1);
            return true;
        }
        return false;
    }

    bool deskbar_start_open() noexcept { return start_open; }

    // Key routing while the Start menu is open (issue #129). Called from
    // winman's key subscriber with the set-1 scancode + decoded ascii —
    // the menu owns the whole keyboard: printable chars feed the search
    // field, Backspace edits, Up/Down move the highlight, Enter launches
    // the selected (or top) result and Esc clears the query, then a
    // second Esc closes the menu.
    bool deskbar_handle_key(hcpp::u16 code, char ascii) noexcept {
        if (!start_open) {
            return false;
        }
        switch (code) {
            case key_escape:
                if (search_len > 0) {
                    search_clear();
                } else {
                    deskbar_close_start();
                }
                return true;
            case key_enter: {
                const hcpp::u32 rows = menu_result_rows();
                if (rows > 0) {
                    if (search_sel >= rows) {
                        search_sel = 0;
                    }
                    launch_result(search_sel);
                }
                deskbar_close_start();
                return true;
            }
            case key_backspace:
                search_pop();
                return true;
            case key_up:
            case key_down: {
                const hcpp::u32 rows = menu_result_rows();
                if (rows > 1) {
                    if (code == key_up) {
                        search_sel =
                            (search_sel + rows - 1) % rows;
                    } else {
                        search_sel = (search_sel + 1) % rows;
                    }
                    invalidate(rui_paint_bounds(compute_menu()));
                }
                return true;
            }
            default:
                break;
        }
        if (ascii >= ' ' && ascii < 127) {
            search_push(ascii);
        }
        return true;
    }

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
