#include "kernel/win/winman.h"

#include "kernel/font/hypefont.h"
#include "kernel/functionemitkeys.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget.h"
#include "kernel/win/deskicons.h"

namespace hype::win {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Point;
        using hype::ui::Rect;
        using hype::ui::Size;
        using hype::ui::Theme;

        constexpr hcpp::i32 chrome_btn = 18;
        constexpr hcpp::i32 chrome_gap = 6;
        constexpr hcpp::i32 chrome_pad = 8;
        // Windows-style caption buttons: ~26-30 px hit boxes, flush together.
        constexpr hcpp::i32 ctl_btn_w = 30;
        constexpr hcpp::i32 ctl_btn_h = 24;
        // Aero Snap zones: edge proximity 16 px, corner boxes 32 px.
        constexpr hcpp::i32 snap_edge = 16;
        constexpr hcpp::i32 snap_corner = 32;
        constexpr hcpp::i32 menu_width = 140;
        constexpr hcpp::i32 menu_item_h = 22;
        constexpr hcpp::i32 menu_pad = 4;
        constexpr hcpp::u32 menu_item_count = 3;
        constexpr hcpp::i32 ctx_menu_width = 180;
        constexpr hcpp::i32 ctx_item_h = 28;
        constexpr hcpp::i32 ctx_sep_h = 9;
        constexpr hcpp::i32 cascade_w = 520;
        constexpr hcpp::i32 cascade_h = 340;
        constexpr hcpp::u32 cascade_wrap = 6;
        // Alt-Tab switcher metrics.
        constexpr hcpp::i32 sw_item_w = 148;
        constexpr hcpp::i32 sw_item_h = 44;
        constexpr hcpp::i32 sw_gap = 6;
        constexpr hcpp::i32 sw_pad = 12;
        // Set-1 scancodes used by the switcher.
        constexpr hcpp::u16 key_escape = 1;
        constexpr hcpp::u16 key_tab = 15;
        constexpr hcpp::u16 key_alt = 56;
        // Titlebar hot-tracking part ids.
        constexpr hcpp::u8 hot_none = 0;
        constexpr hcpp::u8 hot_menu = 1;
        constexpr hcpp::u8 hot_min = 2;
        constexpr hcpp::u8 hot_max = 3;
        constexpr hcpp::u8 hot_close = 4;

        const char* const menu_items[menu_item_count] = {"Move", "Minimize",
                                                         "Close"};

        Win slots[wm_max_windows];
        hcpp::u32 z_counter = 0;
        hcpp::u32 cascade_index = 0;
        hype::ui::OverlayHost overlays;
        hcpp::u32 menu_win = 0;
        Point menu_anchor{};
        Rect menu_screen{0, 0, 1024, 768};
        WmCtxItem ctx_items[wm_ctx_max_items];
        hcpp::u32 ctx_count = 0;
        Point ctx_anchor{};
        WmCtxPick ctx_pick = nullptr;
        void* ctx_user = nullptr;
        bool ctx_active = false;
        Rect damage{};
        bool damaged = false;
        bool damage_full = false;
        // Titlebar drag + snap-preview state.
        hcpp::u32 drag_win = 0;
        Point grab_off{};
        bool drag_engaged = false;
        SnapZone drag_zone = SnapZone::None;
        Rect preview{};
        bool preview_on = false;
        // Caption-button hover state.
        hcpp::u32 hover_win = 0;
        hcpp::u8 hover_part = hot_none;
        // Alt-Tab switcher state.
        bool switcher_open = false;
        hcpp::u32 switcher_ids[wm_max_windows];
        hcpp::u32 switcher_count = 0;
        hcpp::u32 switcher_sel = 0;
        bool keys_hooked = false;
        // XAU layer hooks (kernel/win/xau.cc): paint reroutes window
        // content rendering to WndProc dispatch; event routes wm events.
        WmXauPaintHook xau_paint_hook = nullptr;
        WmXauEventHook xau_event_hook = nullptr;

        void damage_add(const Rect& region) noexcept {
            if (region.empty()) {
                return;
            }
            damage = damaged ? damage.united(region) : region;
            damaged = true;
        }

        Rect chrome_band(const Rect& frame) noexcept {
            return {frame.x, frame.y, frame.w, wm_chrome_height + 12};
        }

        void copy_name(char* dst, const char* src) noexcept {
            hcpp::size_t i = 0;
            while (i < 23 && src[i] != '\0') {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = '\0';
        }

        bool name_equals(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        Win* slot_for(hcpp::u32 id) noexcept {
            if (id == 0 || id > wm_max_windows) {
                return nullptr;
            }
            Win& w = slots[id - 1];
            return w.id == id ? &w : nullptr;
        }

        Rect chrome_strip(const Win& w) noexcept {
            return {w.frame.x + 1, w.frame.y + 1, w.frame.w - 2,
                    wm_chrome_height};
        }

        Rect close_btn_rect(const Win& w) noexcept {
            const Rect strip = chrome_strip(w);
            return {strip.right() - chrome_pad - ctl_btn_w,
                    strip.y + (strip.h - ctl_btn_h) / 2, ctl_btn_w,
                    ctl_btn_h};
        }

        Rect max_btn_rect(const Win& w) noexcept {
            const Rect close = close_btn_rect(w);
            return {close.x - ctl_btn_w, close.y, ctl_btn_w, ctl_btn_h};
        }

        Rect min_btn_rect(const Win& w) noexcept {
            const Rect max = max_btn_rect(w);
            return {max.x - ctl_btn_w, max.y, ctl_btn_w, ctl_btn_h};
        }

        Rect menu_btn_rect(const Win& w) noexcept {
            const Rect min = min_btn_rect(w);
            return {min.x - chrome_gap - chrome_btn,
                    min.y + (min.h - chrome_btn) / 2, chrome_btn, chrome_btn};
        }

        // Work area = screen minus the bottom deskbar band; maximize and
        // every snap target derive from it (appbar-style reserved edge).
        Rect work_area() noexcept {
            return {menu_screen.x, menu_screen.y, menu_screen.w,
                    menu_screen.h - hype::ui::deskbar_height};
        }

        const char* zone_name(SnapZone zone) noexcept {
            switch (zone) {
                case SnapZone::Left: return "left";
                case SnapZone::Right: return "right";
                case SnapZone::Top: return "top";
                case SnapZone::Nw: return "nw";
                case SnapZone::Ne: return "ne";
                case SnapZone::Sw: return "sw";
                case SnapZone::Se: return "se";
                default: return "none";
            }
        }

        SnapZone zone_at(hcpp::i32 x, hcpp::i32 y) noexcept {
            const Rect area = work_area();
            // Corner boxes (2x edge threshold) take precedence over edges.
            const bool near_l = x < area.x + snap_corner;
            const bool near_r = x >= area.right() - snap_corner;
            const bool near_t = y < area.y + snap_corner;
            const bool near_b = y >= area.bottom() - snap_corner;
            if (near_l && near_t) {
                return SnapZone::Nw;
            }
            if (near_r && near_t) {
                return SnapZone::Ne;
            }
            if (near_l && near_b) {
                return SnapZone::Sw;
            }
            if (near_r && near_b) {
                return SnapZone::Se;
            }
            if (x < area.x + snap_edge) {
                return SnapZone::Left;
            }
            if (x >= area.right() - snap_edge) {
                return SnapZone::Right;
            }
            if (y < area.y + snap_edge) {
                return SnapZone::Top;
            }
            return SnapZone::None;
        }

        Rect zone_rect(SnapZone zone) noexcept {
            const Rect area = work_area();
            const hcpp::i32 hw = area.w / 2;
            const hcpp::i32 hh = area.h / 2;
            switch (zone) {
                case SnapZone::Left:
                    return {area.x, area.y, hw, area.h};
                case SnapZone::Right:
                    return {area.x + hw, area.y, area.w - hw, area.h};
                case SnapZone::Top:
                    return area;
                case SnapZone::Nw:
                    return {area.x, area.y, hw, hh};
                case SnapZone::Ne:
                    return {area.x + hw, area.y, area.w - hw, hh};
                case SnapZone::Sw:
                    return {area.x, area.y + hh, hw, area.h - hh};
                case SnapZone::Se:
                    return {area.x + hw, area.y + hh, area.w - hw,
                            area.h - hh};
                default:
                    return {};
            }
        }

        void preview_clear() noexcept {
            if (preview_on) {
                damage_add(preview);
                preview_on = false;
            }
        }

        Rect content_for(const Rect& frame) noexcept {
            const hcpp::i32 top = 1 + wm_chrome_height + 1;
            return {frame.x + 1, frame.y + top, frame.w - 2,
                    frame.h - top - 1};
        }

        Rect menu_rect_now() noexcept {
            Rect r{menu_anchor.x, menu_anchor.y, menu_width,
                   menu_pad * 2 +
                       static_cast<hcpp::i32>(menu_item_count) * menu_item_h};
            if (r.right() > menu_screen.right() - 2) {
                r.x = menu_screen.right() - 2 - r.w;
            }
            if (r.bottom() > menu_screen.bottom() - 2) {
                r.y = menu_screen.bottom() - 2 - r.h;
            }
            if (r.x < menu_screen.x + 2) {
                r.x = menu_screen.x + 2;
            }
            if (r.y < menu_screen.y + 2) {
                r.y = menu_screen.y + 2;
            }
            return r;
        }

        hcpp::i32 menu_item_at(Point p) noexcept {
            const Rect r = menu_rect_now();
            if (!r.contains(p)) {
                return -1;
            }
            const hcpp::i32 idx = (p.y - r.y - menu_pad) / menu_item_h;
            if (idx < 0 || idx >= static_cast<hcpp::i32>(menu_item_count)) {
                return -1;
            }
            return idx;
        }

        void paint_label(Canvas& canvas, hcpp::i32 x, hcpp::i32 y,
                         hcpp::StringView text, hcpp::u32 rgb,
                         hcpp::u32 px) noexcept {
            if (hype::font::hypefont_ready()) {
                hype::ui::draw_text_sdf_styled(canvas, x, y, text,
                                               canvas.color(rgb), px, 96,
                                               false);
                return;
            }
            const hcpp::fonts::FontFace* face =
                hype::ui::theme_active().resolved_font();
            if (face == nullptr) {
                return;
            }
            canvas.draw_text(x, y + face->baseline, text, canvas.color(rgb),
                             *face);
        }

        class WinMenu : public hype::ui::Widget {
        public:
            Size measure(Size available) noexcept override {
                (void)available;
                return preferred_size();
            }

            void layout(const Rect& rect) noexcept override {
                (void)rect;
                set_bounds(menu_rect_now());
            }

            void paint(Canvas& canvas) noexcept override {
                const Rect box = bounds();
                if (box.empty()) {
                    return;
                }
                const Theme& theme = hype::ui::theme_active();
                hype::ui::rui_panel(canvas, box, hype::ui::rui_elev_panel);
                const hcpp::i32 line = hype::font::hypefont_ready()
                                           ? hype::font::text_line_height(13)
                                           : 16;
                for (hcpp::u32 i = 0; i < menu_item_count; ++i) {
                    const Rect row{box.x + 1,
                                   box.y + menu_pad +
                                       static_cast<hcpp::i32>(i) * menu_item_h,
                                   box.w - 2, menu_item_h};
                    canvas.fill_rect(
                        row, canvas.color(hype::ui::rui_lift(
                                 theme.card, hype::ui::rui_elev_panel)));
                    hcpp::i32 top = row.y + (row.h - line) / 2;
                    if (top < row.y) {
                        top = row.y;
                    }
                    paint_label(canvas, row.x + 10, top, menu_items[i],
                                theme.card_fg, 13);
                }
                if (hype::ui::rui_dark_surface()) {
                    hype::gfx::gfx_inner_glow_edge(
                        canvas, box.inset(hype::ui::Insets::uniform(1)),
                        static_cast<hcpp::i32>(theme.radius_lg), theme.accent);
                }
            }
        };

        WinMenu menu_widget;

        hcpp::i32 ctx_row_h(const WmCtxItem& item) noexcept {
            return (item.flags & wm_ctx_separator) != 0 ? ctx_sep_h
                                                        : ctx_item_h;
        }

        Rect ctx_rect_now() noexcept {
            hcpp::i32 h = menu_pad * 2;
            for (hcpp::u32 i = 0; i < ctx_count; ++i) {
                h += ctx_row_h(ctx_items[i]);
            }
            Rect r{ctx_anchor.x, ctx_anchor.y, ctx_menu_width, h};
            if (r.right() > menu_screen.right() - 2) {
                r.x = menu_screen.right() - 2 - r.w;
            }
            if (r.bottom() > menu_screen.bottom() - 2) {
                r.y = menu_screen.bottom() - 2 - r.h;
            }
            if (r.x < menu_screen.x + 2) {
                r.x = menu_screen.x + 2;
            }
            if (r.y < menu_screen.y + 2) {
                r.y = menu_screen.y + 2;
            }
            return r;
        }

        // Returns the item index under `p`, including separator rows so
        // callers can tell "inside the menu" from "outside".
        hcpp::i32 ctx_item_at(Point p) noexcept {
            const Rect r = ctx_rect_now();
            if (!r.contains(p)) {
                return -1;
            }
            hcpp::i32 row_y = r.y + menu_pad;
            for (hcpp::u32 i = 0; i < ctx_count; ++i) {
                const hcpp::i32 h = ctx_row_h(ctx_items[i]);
                if (p.y >= row_y && p.y < row_y + h) {
                    return static_cast<hcpp::i32>(i);
                }
                row_y += h;
            }
            return -1;
        }

        class CtxMenu : public hype::ui::Widget {
        public:
            Size measure(Size available) noexcept override {
                (void)available;
                return preferred_size();
            }

            void layout(const Rect& rect) noexcept override {
                (void)rect;
                set_bounds(ctx_rect_now());
            }

            void paint(Canvas& canvas) noexcept override {
                const Rect box = bounds();
                if (box.empty()) {
                    return;
                }
                const Theme& theme = hype::ui::theme_active();
                hype::ui::rui_panel(canvas, box, hype::ui::rui_elev_panel);
                const hcpp::i32 line = hype::font::hypefont_ready()
                                           ? hype::font::text_line_height(13)
                                           : 16;
                hcpp::i32 row_y = box.y + menu_pad;
                for (hcpp::u32 i = 0; i < ctx_count; ++i) {
                    const WmCtxItem& item = ctx_items[i];
                    const hcpp::i32 h = ctx_row_h(item);
                    const Rect row{box.x + 1, row_y, box.w - 2, h};
                    if ((item.flags & wm_ctx_separator) != 0) {
                        canvas.fill_rect(
                            Rect{row.x + 8, row.y + h / 2, row.w - 16, 1},
                            canvas.color(theme.border));
                    } else {
                        canvas.fill_rect(
                            row, canvas.color(hype::ui::rui_lift(
                                     theme.card, hype::ui::rui_elev_panel)));
                        hcpp::i32 top = row.y + (row.h - line) / 2;
                        if (top < row.y) {
                            top = row.y;
                        }
                        paint_label(
                            canvas, row.x + 10, top,
                            item.label != nullptr ? item.label : "",
                            (item.flags & wm_ctx_disabled) != 0
                                ? theme.muted_fg
                                : theme.card_fg,
                            13);
                    }
                    row_y += h;
                }
                if (hype::ui::rui_dark_surface()) {
                    hype::gfx::gfx_inner_glow_edge(
                        canvas, box.inset(hype::ui::Insets::uniform(1)),
                        static_cast<hcpp::i32>(theme.radius_lg),
                        theme.accent);
                }
            }
        };

        CtxMenu ctx_widget;

        void paint_chrome(Canvas& canvas, const Win& w) noexcept {
            const Theme& theme = hype::ui::theme_active();
            const Rect box = w.frame;
            if (box.empty()) {
                return;
            }
            const bool focused = w.state == WinState::Focused;
            hype::ui::rui_panel(canvas, box, hype::ui::rui_elev_window);
            const Rect strip = chrome_strip(w);
            if (strip.empty()) {
                return;
            }
            const hcpp::i32 radius =
                static_cast<hcpp::i32>(theme.radius_lg);
            hcpp::i32 band_h = strip.h + radius;
            const hcpp::i32 max_h = box.bottom() - 1 - strip.y;
            if (band_h > max_h) {
                band_h = max_h;
            }
            hype::ui::rui_rounded_gradient(
                canvas, Rect{strip.x, strip.y, strip.w, band_h}, radius,
                hype::ui::rui_lift(theme.muted, focused ? 2 : 1), theme.muted);
            canvas.fill_rounded_rect(
                Rect{strip.x + 4, strip.bottom() - 1, strip.w - 8, 2}, 1,
                canvas.color(focused ? theme.accent : theme.border));

            const hcpp::i32 cy = strip.y + strip.h / 2;
            canvas.fill_circle(strip.x + chrome_pad + 3, cy, 3,
                               canvas.color(theme.accent));

            const bool hot = hover_win == w.id;
            const hcpp::u32 glyph = canvas.color(theme.card_fg);

            const Rect menu_btn = menu_btn_rect(w);
            hype::ui::rui_control_bg(
                canvas, menu_btn,
                hot && hover_part == hot_menu
                    ? hype::ui::rui_lift(theme.secondary, 2)
                    : theme.secondary,
                hot && hover_part == hot_menu ? theme.accent
                                              : theme.border);
            for (hcpp::i32 k = 0; k < 3; ++k) {
                canvas.fill_rect(
                    Rect{menu_btn.x + 4, menu_btn.y + 4 + k * 4,
                         menu_btn.w - 8, 2},
                    glyph);
            }

            // Minimize "_" caption button.
            const Rect min_btn = min_btn_rect(w);
            hype::ui::rui_control_bg(
                canvas, min_btn,
                hot && hover_part == hot_min
                    ? hype::ui::rui_lift(theme.secondary, 2)
                    : theme.secondary,
                hot && hover_part == hot_min ? theme.accent
                                             : theme.border);
            canvas.fill_rect(Rect{min_btn.x + 9, min_btn.bottom() - 8,
                                  min_btn.w - 18, 2},
                             glyph);

            // Maximize "square" / restore "overlapping squares" glyph.
            const Rect max_btn = max_btn_rect(w);
            hype::ui::rui_control_bg(
                canvas, max_btn,
                hot && hover_part == hot_max
                    ? hype::ui::rui_lift(theme.secondary, 2)
                    : theme.secondary,
                hot && hover_part == hot_max ? theme.accent
                                             : theme.border);
            if (w.maximized) {
                canvas.stroke_rect(
                    Rect{max_btn.x + 11, max_btn.y + 6, 10, 8}, glyph, 1);
                canvas.stroke_rect(
                    Rect{max_btn.x + 8, max_btn.y + 9, 10, 9}, glyph, 1);
            } else {
                canvas.stroke_rect(
                    Rect{max_btn.x + 9, max_btn.y + 7, 12, 10}, glyph, 1);
            }

            const Rect close_btn = close_btn_rect(w);
            hype::ui::rui_control_bg(canvas, close_btn,
                                     hot && hover_part == hot_close
                                         ? theme.destructive
                                         : hype::ui::rui_shade(
                                               theme.destructive, 40),
                                     theme.destructive);
            canvas.draw_line(close_btn.x + 9, close_btn.y + 6,
                             close_btn.right() - 10, close_btn.bottom() - 7,
                             canvas.color(theme.primary_fg));
            canvas.draw_line(close_btn.x + 9, close_btn.bottom() - 7,
                             close_btn.right() - 10, close_btn.y + 6,
                             canvas.color(theme.primary_fg));

            const hcpp::i32 title_x = strip.x + chrome_pad + 12;
            const hcpp::i32 max_text =
                menu_btn.x - chrome_gap - title_x;
            if (max_text <= 0) {
                return;
            }
            const hcpp::i32 line = hype::font::hypefont_ready()
                                       ? hype::font::text_line_height(13)
                                       : 16;
            hcpp::i32 top = strip.y + (strip.h - line) / 2;
            if (top < strip.y) {
                top = strip.y;
            }
            paint_label(canvas, title_x, top,
                        w.caption[0] != '\0' ? w.caption : w.app_name,
                        focused ? theme.card_fg : theme.muted_fg, 13);
        }

        void sort_order(hcpp::u32* order, hcpp::u32 count,
                        bool ascending) noexcept {
            for (hcpp::u32 i = 1; i < count; ++i) {
                const hcpp::u32 key = order[i];
                hcpp::u32 j = i;
                while (j > 0 &&
                       (ascending ? slots[order[j - 1]].z > slots[key].z
                                  : slots[order[j - 1]].z < slots[key].z)) {
                    order[j] = order[j - 1];
                    --j;
                }
                order[j] = key;
            }
        }

        hcpp::u32 collect_order(hcpp::u32* order, bool ascending) noexcept {
            hcpp::u32 n = 0;
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 &&
                    slots[i].state != WinState::Minimized) {
                    order[n++] = i;
                }
            }
            sort_order(order, n, ascending);
            return n;
        }

        void set_frame(Win& w, const Rect& next) noexcept {
            const Rect prev = w.frame;
            damage_add(w.frame);
            w.frame = next;
            w.content_rect = content_for(next);
            damage_add(next);
            if (xau_event_hook != nullptr && w.id != 0) {
                if (next.x != prev.x || next.y != prev.y) {
                    xau_event_hook(w.id, wm_xau_ev_move, next.x, next.y);
                }
                if (next.w != prev.w || next.h != prev.h) {
                    xau_event_hook(w.id, wm_xau_ev_size, next.w, next.h);
                }
            }
        }

        void drag_finish() noexcept {
            Win* w = slot_for(drag_win);
            const SnapZone zone = drag_zone;
            preview_clear();
            drag_zone = SnapZone::None;
            drag_win = 0;
            drag_engaged = false;
            if (w != nullptr && zone != SnapZone::None) {
                // The frame is floating again after drag begin; remember it
                // as the restore rect, then commit the snapped geometry.
                w->restore = w->frame;
                set_frame(*w, zone_rect(zone));
                if (zone == SnapZone::Top) {
                    w->maximized = true;
                    w->snap = SnapZone::None;
                    serial::write_line("win maximized=1");
                } else {
                    w->maximized = false;
                    w->snap = zone;
                }
                serial::write("snap zone=");
                serial::write_line(zone_name(zone));
            }
        }

        void update_hover(hcpp::i32 x, hcpp::i32 y) noexcept {
            hcpp::u32 win = 0;
            hcpp::u8 part = hot_none;
            const Point p{x, y};
            hcpp::u32 order[wm_max_windows];
            const hcpp::u32 n = collect_order(order, false);
            for (hcpp::u32 k = 0; k < n; ++k) {
                Win& w = slots[order[k]];
                if (!chrome_strip(w).contains(p)) {
                    continue;
                }
                win = w.id;
                if (close_btn_rect(w).contains(p)) {
                    part = hot_close;
                } else if (max_btn_rect(w).contains(p)) {
                    part = hot_max;
                } else if (min_btn_rect(w).contains(p)) {
                    part = hot_min;
                } else if (menu_btn_rect(w).contains(p)) {
                    part = hot_menu;
                }
                break;
            }
            if (win == hover_win && part == hover_part) {
                return;
            }
            Win* old = slot_for(hover_win);
            if (old != nullptr) {
                damage_add(chrome_strip(*old));
            }
            hover_win = win;
            hover_part = part;
            Win* next = slot_for(hover_win);
            if (next != nullptr) {
                damage_add(chrome_strip(*next));
            }
        }

        // Alt-Tab switcher: MRU list = visible windows by descending z,
        // minimized windows appended last (also descending z).
        hcpp::u32 collect_mru(hcpp::u32* ids) noexcept {
            hcpp::u32 order[wm_max_windows];
            hcpp::u32 n = collect_order(order, false);
            for (hcpp::u32 i = 0; i < n; ++i) {
                ids[i] = slots[order[i]].id;
            }
            hcpp::u32 mins[wm_max_windows];
            hcpp::u32 m = 0;
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 &&
                    slots[i].state == WinState::Minimized) {
                    mins[m++] = i;
                }
            }
            sort_order(mins, m, false);
            for (hcpp::u32 i = 0; i < m; ++i) {
                ids[n + i] = slots[mins[i]].id;
            }
            return n + m;
        }

        hcpp::i32 switcher_item_w(hcpp::u32 n) noexcept {
            const Rect area = work_area();
            const hcpp::i32 max_w = area.w - 64;
            hcpp::i32 w = sw_item_w;
            const hcpp::i32 need =
                sw_pad * 2 + static_cast<hcpp::i32>(n) * w +
                (static_cast<hcpp::i32>(n) - 1) * sw_gap;
            if (need > max_w && n > 0) {
                w = (max_w - sw_pad * 2 -
                     (static_cast<hcpp::i32>(n) - 1) * sw_gap) /
                    static_cast<hcpp::i32>(n);
                if (w < 40) {
                    w = 40;
                }
            }
            return w;
        }

        Rect switcher_rect() noexcept {
            const Rect area = work_area();
            const hcpp::u32 n = switcher_count > 0 ? switcher_count : 1;
            const hcpp::i32 item_w = switcher_item_w(n);
            const hcpp::i32 w =
                sw_pad * 2 + static_cast<hcpp::i32>(n) * item_w +
                (static_cast<hcpp::i32>(n) - 1) * sw_gap;
            const hcpp::i32 h = sw_pad * 2 + sw_item_h;
            return {area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w,
                    h};
        }

        void alttab_marker() noexcept {
            serial::write("alttab open=1 sel=");
            serial::write_dec32(switcher_sel);
            serial::write("\r\n");
        }

        void alttab_begin() noexcept {
            switcher_count = collect_mru(switcher_ids);
            if (switcher_count == 0) {
                return;
            }
            switcher_sel = switcher_count > 1 ? 1 : 0;
            switcher_open = true;
            damage_add(switcher_rect());
            alttab_marker();
        }

        void alttab_step(hcpp::i32 dir) noexcept {
            if (!switcher_open || switcher_count == 0) {
                return;
            }
            switcher_sel = static_cast<hcpp::u32>(
                (static_cast<hcpp::i32>(switcher_sel) + dir +
                 static_cast<hcpp::i32>(switcher_count)) %
                static_cast<hcpp::i32>(switcher_count));
            damage_add(switcher_rect());
            alttab_marker();
        }

        void alttab_close(bool commit) noexcept {
            const hcpp::u32 id =
                switcher_open && switcher_sel < switcher_count
                    ? switcher_ids[switcher_sel]
                    : 0;
            if (switcher_open) {
                damage_add(switcher_rect());
            }
            switcher_open = false;
            serial::write_line("alttab open=0");
            if (commit && id != 0) {
                wm_set_minimized(id, false);
                wm_focus(id);
            }
        }

        void alttab_sync() noexcept {
            // A window set change while the switcher is up: rebuild the
            // MRU list and keep the selection in range.
            if (!switcher_open) {
                return;
            }
            switcher_count = collect_mru(switcher_ids);
            if (switcher_count == 0) {
                switcher_open = false;
                serial::write_line("alttab open=0");
                return;
            }
            if (switcher_sel >= switcher_count) {
                switcher_sel = switcher_count - 1;
            }
            damage_add(switcher_rect());
        }

        void wm_key_event(void*, const input::InputEvent& e) noexcept {
            if (e.type != input::InputEventType::Key) {
                return;
            }
            // Locked session owns the keyboard: the lock handler consumes
            // Enter/click and swallows everything else, so WM chords
            // (Alt-Tab, switcher keys) must not run underneath.
            if (hype::session::session_locked()) {
                return;
            }
            const bool pressed =
                e.value !=
                static_cast<hcpp::u32>(input::KeyState::Released);
            if (e.code == key_alt) {
                if (!pressed && switcher_open) {
                    alttab_close(true);
                }
                return;
            }
            if (!pressed) {
                return;
            }
            if (e.code == key_tab &&
                input::scancode_key_pressed(key_alt)) {
                if (!switcher_open) {
                    alttab_begin();
                } else {
                    alttab_step(input::scancode_shift_active() ? -1 : 1);
                }
            } else if (e.code == key_escape && switcher_open) {
                alttab_close(false);
            }
        }

        void ensure_key_hook() noexcept {
            if (keys_hooked) {
                return;
            }
            keys_hooked = input::subscribe_keys(&wm_key_event, nullptr);
        }

        void paint_switcher(Canvas& canvas) noexcept {
            if (!switcher_open || switcher_count == 0) {
                return;
            }
            const Theme& theme = hype::ui::theme_active();
            const Rect panel = switcher_rect();
            hype::ui::rui_panel(canvas, panel, hype::ui::rui_elev_panel);
            const hcpp::i32 item_w = switcher_item_w(switcher_count);
            const hcpp::i32 line = hype::font::hypefont_ready()
                                       ? hype::font::text_line_height(13)
                                       : 16;
            for (hcpp::u32 i = 0; i < switcher_count; ++i) {
                const Rect item{
                    panel.x + sw_pad +
                        static_cast<hcpp::i32>(i) * (item_w + sw_gap),
                    panel.y + sw_pad, item_w, sw_item_h};
                const bool sel = i == switcher_sel;
                hype::ui::rui_control_bg(
                    canvas, item,
                    sel ? hype::ui::rui_lift(theme.secondary, 2)
                        : theme.secondary,
                    sel ? theme.accent : theme.border);
                const Win* w = slot_for(switcher_ids[i]);
                const hcpp::i32 cy = item.y + item.h / 2;
                canvas.fill_circle(item.x + 14, cy, 4,
                                   canvas.color(w != nullptr &&
                                                        w->state ==
                                                            WinState::Minimized
                                                    ? theme.muted_fg
                                                    : theme.accent));
                if (w == nullptr) {
                    continue;
                }
                // Truncate the title so it cannot bleed into the next chip.
                char label[24];
                hcpp::i32 max_chars = (item_w - 34) / 7;
                if (max_chars < 4) {
                    max_chars = 4;
                }
                if (max_chars > 23) {
                    max_chars = 23;
                }
                hcpp::i32 len = 0;
                while (len < max_chars && w->app_name[len] != '\0') {
                    label[len] = w->app_name[len];
                    ++len;
                }
                label[len] = '\0';
                hcpp::i32 top = cy - line / 2;
                if (top < item.y + 4) {
                    top = item.y + 4;
                }
                paint_label(canvas, item.x + 26, top,
                            hcpp::StringView{label},
                            sel ? theme.card_fg : theme.muted_fg, 13);
            }
            if (hype::ui::rui_dark_surface()) {
                hype::gfx::gfx_inner_glow_edge(
                    canvas, panel.inset(hype::ui::Insets::uniform(1)),
                    static_cast<hcpp::i32>(theme.radius_lg), theme.accent);
            }
        }

    }

    hcpp::u32 wm_open(const char* app_name, Rect frame) noexcept {
        if (app_name == nullptr || app_name[0] == '\0') {
            return 0;
        }
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0 && name_equals(slots[i].app_name, app_name)) {
                wm_focus(slots[i].id);
                return slots[i].id;
            }
        }
        hcpp::u32 slot = wm_max_windows;
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id == 0) {
                slot = i;
                break;
            }
        }
        if (slot >= wm_max_windows) {
            return 0;
        }
        if (frame.empty()) {
            const hcpp::u32 step = cascade_index % cascade_wrap;
            frame = {64 + static_cast<hcpp::i32>(step) * wm_cascade_step,
                     48 + static_cast<hcpp::i32>(step) * wm_cascade_step,
                     cascade_w, cascade_h};
            ++cascade_index;
        }
        Win& w = slots[slot];
        w = Win{};
        w.id = slot + 1;
        copy_name(w.app_name, app_name);
        w.frame = frame;
        w.content_rect = content_for(frame);
        w.state = WinState::Normal;
        w.z = ++z_counter;
        w.ctx = hxrt::AppCtx{};
        w.ctx.region = w.content_rect;
        wm_focus(w.id);
        damage_add(frame);
        return w.id;
    }

    bool wm_close(hcpp::u32 id) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        const hxrt::HxerHeader* app = hxrt::hxrt_find(w->app_name);
        if (app != nullptr && app->on_close != nullptr) {
            app->on_close(w->ctx);
        }
        // XAU windows get CLOSE + DESTROY through the event hook; the
        // hook is re-entrancy safe (xau_destroy inside CLOSE lands back
        // here on the already-unbound slot).
        if (xau_event_hook != nullptr) {
            xau_event_hook(id, wm_xau_ev_close, 0, 0);
        }
        const bool was_focused = w->state == WinState::Focused;
        if (menu_win == id) {
            wm_menu_close();
        }
        if (hover_win == id) {
            hover_win = 0;
            hover_part = hot_none;
        }
        if (drag_win == id) {
            preview_clear();
            drag_zone = SnapZone::None;
            drag_win = 0;
            drag_engaged = false;
        }
        const Rect gone = w->frame;
        *w = Win{};
        damage_add(gone);
        alttab_sync();
        if (was_focused) {
            hcpp::u32 top_order[wm_max_windows];
            const hcpp::u32 n = collect_order(top_order, false);
            if (n > 0) {
                wm_focus(slots[top_order[0]].id);
            }
        }
        return true;
    }

    bool wm_focus(hcpp::u32 id) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0 && slots[i].state == WinState::Focused) {
                damage_add(chrome_band(slots[i].frame));
                slots[i].state = WinState::Normal;
                if (xau_event_hook != nullptr) {
                    xau_event_hook(slots[i].id, wm_xau_ev_focus, 0, 0);
                }
            }
        }
        if (w->state != WinState::Focused) {
            if (w->state == WinState::Minimized) {
                damage_add(w->frame);
            }
            w->state = WinState::Focused;
            damage_add(chrome_band(w->frame));
        }
        if (xau_event_hook != nullptr) {
            xau_event_hook(id, wm_xau_ev_focus, 1, 0);
        }
        w->z = ++z_counter;
        return true;
    }

    bool wm_set_minimized(hcpp::u32 id, bool minimized) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        if (minimized) {
            if (w->state == WinState::Minimized) {
                return true;
            }
            const bool was_focused = w->state == WinState::Focused;
            w->state = WinState::Minimized;
            damage_add(w->frame);
            if (xau_event_hook != nullptr) {
                xau_event_hook(id, wm_xau_ev_minimize, 1, 0);
            }
            if (was_focused) {
                hcpp::u32 top_order[wm_max_windows];
                const hcpp::u32 n = collect_order(top_order, false);
                if (n > 0) {
                    wm_focus(slots[top_order[0]].id);
                }
            }
            return true;
        }
        if (w->state == WinState::Minimized) {
            w->state = WinState::Normal;
            damage_add(w->frame);
            if (xau_event_hook != nullptr) {
                xau_event_hook(id, wm_xau_ev_minimize, 0, 0);
            }
        }
        alttab_sync();
        return true;
    }

    bool wm_set_maximized(hcpp::u32 id, bool maximized) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        if (maximized) {
            if (w->state == WinState::Minimized) {
                w->state = WinState::Normal;
            }
            if (w->maximized) {
                return true;
            }
            // Keep the floating frame as the restore rect unless a snap
            // already saved one.
            if (w->snap == SnapZone::None) {
                w->restore = w->frame;
            }
            set_frame(*w, work_area());
            w->maximized = true;
            w->snap = SnapZone::None;
            serial::write_line("win maximized=1");
            if (xau_event_hook != nullptr) {
                xau_event_hook(id, wm_xau_ev_maximize, 1, 0);
            }
            return true;
        }
        if (!w->maximized && w->snap == SnapZone::None) {
            return true;
        }
        const bool was_max = w->maximized;
        Rect r = w->restore;
        if (r.empty()) {
            r = w->frame;
        }
        set_frame(*w, r);
        w->maximized = false;
        w->snap = SnapZone::None;
        if (was_max) {
            serial::write_line("win maximized=0");
        } else {
            serial::write_line("snap zone=none");
        }
        if (xau_event_hook != nullptr) {
            xau_event_hook(id, wm_xau_ev_maximize, 0, 0);
        }
        return true;
    }

    bool wm_snap(hcpp::u32 id, SnapZone zone) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        if (zone == SnapZone::None) {
            return wm_set_maximized(id, false);
        }
        if (w->state == WinState::Minimized) {
            w->state = WinState::Normal;
        }
        if (!w->maximized && w->snap == SnapZone::None) {
            w->restore = w->frame;
        }
        set_frame(*w, zone_rect(zone));
        if (zone == SnapZone::Top) {
            w->maximized = true;
            w->snap = SnapZone::None;
            serial::write_line("win maximized=1");
        } else {
            w->maximized = false;
            w->snap = zone;
        }
        serial::write("snap zone=");
        serial::write_line(zone_name(zone));
        return true;
    }

    void wm_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return;
        }
        ensure_key_hook();
        const Rect screen{0, 0, static_cast<hcpp::i32>(fb.width()),
                          static_cast<hcpp::i32>(fb.height())};
        menu_screen = screen;
        const Rect clip = damage_full || overlays.is_open()
                              ? screen
                              : damage.intersection(screen);
        damage = Rect{};
        damaged = false;
        damage_full = false;
        if (clip.empty()) {
            return;
        }
        Canvas canvas(fb);
        canvas.set_clip(clip);
        hcpp::u32 order[wm_max_windows];
        const hcpp::u32 n = collect_order(order, true);
        const Theme& theme = hype::ui::theme_active();
        for (hcpp::u32 k = 0; k < n; ++k) {
            Win& w = slots[order[k]];
            if (!w.frame.intersects(clip)) {
                continue;
            }
            paint_chrome(canvas, w);
            const Rect region = w.content_rect.intersection(screen);
            const Rect content_clip = region.intersection(clip);
            w.ctx.fb = &fb;
            w.ctx.region = region;
            w.ctx.clip = content_clip;
            // XAU windows render through their WndProc (PAINT message);
            // unowned windows fall back to the .hxer entry point.
            bool xau_painted = false;
            if (xau_paint_hook != nullptr && !content_clip.empty()) {
                xau_painted = xau_paint_hook(w.id, w.ctx);
            }
            if (!content_clip.empty() && !xau_painted &&
                !hxrt::hxrt_exec(w.app_name, w.ctx)) {
                canvas.fill_rect(region, canvas.color(theme.card));
                paint_label(canvas, region.x + 16, region.y + 16,
                            "app unavailable", theme.muted_fg, 13);
            }
            // Glowing accent bottom edge over the painted content (dark UI).
            if (hype::ui::rui_dark_surface()) {
                hype::gfx::gfx_inner_glow_edge(
                    canvas, w.frame.inset(hype::ui::Insets::uniform(1)),
                    static_cast<hcpp::i32>(theme.radius_lg), theme.accent);
            }
        }
        // Translucent snap-preview hint while a titlebar drag hovers a zone.
        if (preview_on && !preview.empty()) {
            canvas.fill_rect_alpha(preview, canvas.color(theme.accent), 90);
            canvas.stroke_rect(preview, canvas.color(theme.accent), 2);
        }
        if (switcher_open) {
            paint_switcher(canvas);
        }
        overlays.paint_overlays(canvas, screen);
    }

    void wm_invalidate(hype::ui::Rect region) noexcept {
        damage_add(region);
    }

    void wm_invalidate_app(const char* app_name) noexcept {
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0 && name_equals(slots[i].app_name, app_name)) {
                damage_add(slots[i].frame);
            }
        }
    }

    void wm_invalidate_all() noexcept { damage_full = true; }

    bool wm_dirty() noexcept { return damaged || damage_full; }

    hype::ui::Rect wm_damage_clip(hype::ui::Rect bounds) noexcept {
        if (!wm_dirty()) {
            return {};
        }
        return damage_full ? bounds : damage.intersection(bounds);
    }

    bool wm_menu_open(hcpp::u32 id) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr) {
            return false;
        }
        const Rect btn = menu_btn_rect(*w);
        menu_anchor = {btn.x, btn.bottom() + 2};
        menu_win = id;
        ctx_active = false;
        ctx_count = 0;
        menu_widget.set_preferred_size(
            Size{menu_width,
                 menu_pad * 2 +
                     static_cast<hcpp::i32>(menu_item_count) * menu_item_h});
        if (!overlays.open(&menu_widget)) {
            menu_win = 0;
            return false;
        }
        damage_full = true;
        return true;
    }

    void wm_menu_close() noexcept {
        damage_full = true;
        overlays.close();
        menu_win = 0;
        ctx_active = false;
        ctx_count = 0;
    }

    bool wm_menu_visible() noexcept {
        return menu_win != 0 && overlays.is_open();
    }

    bool wm_ctx_open(hcpp::i32 x, hcpp::i32 y, const WmCtxItem* items,
                     hcpp::u32 count, WmCtxPick pick, void* user) noexcept {
        if (items == nullptr || count == 0 || pick == nullptr) {
            return false;
        }
        if (count > wm_ctx_max_items) {
            count = wm_ctx_max_items;
        }
        // Both menus share the single overlay slot: drop the window menu.
        if (overlays.is_open()) {
            overlays.close();
        }
        menu_win = 0;
        for (hcpp::u32 i = 0; i < count; ++i) {
            ctx_items[i] = items[i];
        }
        ctx_count = count;
        ctx_anchor = {x, y};
        ctx_pick = pick;
        ctx_user = user;
        hcpp::i32 h = menu_pad * 2;
        for (hcpp::u32 i = 0; i < ctx_count; ++i) {
            h += ctx_row_h(ctx_items[i]);
        }
        ctx_widget.set_preferred_size(Size{ctx_menu_width, h});
        if (!overlays.open(&ctx_widget)) {
            ctx_count = 0;
            return false;
        }
        ctx_active = true;
        damage_full = true;
        serial::write("ctx menu=open items=");
        serial::write_dec32(ctx_count);
        serial::write("\r\n");
        return true;
    }

    void wm_ctx_close() noexcept {
        if (!ctx_active) {
            return;
        }
        damage_full = true;
        overlays.close();
        ctx_active = false;
        ctx_count = 0;
        ctx_pick = nullptr;
        ctx_user = nullptr;
    }

    bool wm_ctx_visible() noexcept {
        return ctx_active && overlays.is_open();
    }

    bool wm_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept {
        if (!pressed) {
            if (drag_win != 0) {
                drag_finish();
                return true;
            }
            // Deliver the button release to the topmost XAU window whose
            // content region contains the pointer.
            if (xau_event_hook != nullptr) {
                const Point p{x, y};
                hcpp::u32 order[wm_max_windows];
                const hcpp::u32 n = collect_order(order, false);
                for (hcpp::u32 k = 0; k < n; ++k) {
                    Win& w = slots[order[k]];
                    if (w.content_rect.contains(p) &&
                        xau_event_hook(w.id, wm_xau_ev_lbuttonup, p.x,
                                       p.y)) {
                        return true;
                    }
                }
            }
            return false;
        }
        const Point p{x, y};
        if (wm_ctx_visible()) {
            const hcpp::i32 item = ctx_item_at(p);
            if (item >= 0) {
                const WmCtxItem it = ctx_items[item];
                if ((it.flags &
                     (wm_ctx_separator | wm_ctx_disabled)) == 0 &&
                    ctx_pick != nullptr) {
                    const hcpp::u32 id = it.id;
                    WmCtxPick pick = ctx_pick;
                    void* user = ctx_user;
                    wm_ctx_close();
                    pick(id, user);
                }
                // Separator/disabled rows keep the menu open.
                return true;
            }
            wm_ctx_close();
            return true;
        }
        if (wm_menu_visible()) {
            const hcpp::u32 target = menu_win;
            const hcpp::i32 item = menu_item_at(p);
            if (item == 2) {
                wm_close(target);
            } else {
                if (item == 0) {
                    serial::write_line("wm move=stub");
                } else if (item == 1) {
                    wm_set_minimized(target, true);
                }
                wm_menu_close();
            }
            return true;
        }
        if (hype::ui::deskbar_handle_mouse(x, y, pressed)) {
            return true;
        }
        hcpp::u32 order[wm_max_windows];
        const hcpp::u32 n = collect_order(order, false);
        for (hcpp::u32 k = 0; k < n; ++k) {
            Win& w = slots[order[k]];
            if (!w.frame.contains(p)) {
                continue;
            }
            if (close_btn_rect(w).contains(p)) {
                return wm_close(w.id);
            }
            if (max_btn_rect(w).contains(p)) {
                return wm_set_maximized(w.id, !w.maximized);
            }
            if (min_btn_rect(w).contains(p)) {
                return wm_set_minimized(w.id, true);
            }
            if (menu_btn_rect(w).contains(p)) {
                return wm_menu_open(w.id);
            }
            if (chrome_strip(w).contains(p)) {
                // Titlebar press: focus and arm the drag engine; snapped or
                // maximized geometry is restored under the cursor on the
                // first motion (Windows behavior).
                wm_focus(w.id);
                drag_win = w.id;
                grab_off = {p.x - w.frame.x, p.y - w.frame.y};
                drag_engaged = false;
                drag_zone = SnapZone::None;
                return true;
            }
            wm_focus(w.id);
            // Press inside the content region: offer it to the app so
            // interactive apps (RevDBGService controls) can react, then
            // repaint the window in case state changed.
            if (w.content_rect.contains(p)) {
                const hxrt::HxerHeader* app = hxrt::hxrt_find(w.app_name);
                if (app != nullptr && app->on_click != nullptr) {
                    app->on_click(w.ctx, p.x, p.y);
                    damage_add(w.frame);
                }
                // Parallel XAU delivery: LBUTTONDOWN to the WndProc.
                if (xau_event_hook != nullptr &&
                    xau_event_hook(w.id, wm_xau_ev_lbuttondown, p.x,
                                   p.y)) {
                    damage_add(w.frame);
                }
            }
            return true;
        }
        // No window and no taskbar zone claimed the press: the click
        // landed on empty desktop — offer it to the icon band.
        return desk_icons_handle_click(x, y);
    }

    bool wm_handle_mouse_r(hcpp::i32 x, hcpp::i32 y) noexcept {
        const Point p{x, y};
        if (wm_ctx_visible()) {
            wm_ctx_close();
            return true;
        }
        if (wm_menu_visible()) {
            wm_menu_close();
            return true;
        }
        // Right-click inside a window frame opens that window's system
        // menu, same overlay path as the chrome menu button.
        hcpp::u32 order[wm_max_windows];
        const hcpp::u32 n = collect_order(order, false);
        for (hcpp::u32 k = 0; k < n; ++k) {
            Win& w = slots[order[k]];
            if (!w.frame.contains(p)) {
                continue;
            }
            wm_focus(w.id);
            // XAU windows get RBUTTONDOWN in the content region instead
            // of the system menu (Windows-style client behavior).
            if (xau_event_hook != nullptr && w.content_rect.contains(p) &&
                xau_event_hook(w.id, wm_xau_ev_rbuttondown, p.x, p.y)) {
                return true;
            }
            wm_menu_open(w.id);
            return true;
        }
        // The taskbar band consumes right-presses without a menu.
        const Rect bar{menu_screen.x,
                       menu_screen.bottom() - hype::ui::deskbar_height,
                       menu_screen.w, hype::ui::deskbar_height};
        if (bar.contains(p)) {
            return true;
        }
        // Empty desktop: icon menu on a cell, desktop menu otherwise.
        return desk_icons_handle_rclick(x, y);
    }

    bool wm_handle_move(hcpp::i32 x, hcpp::i32 y) noexcept {
        bool used = false;
        if (drag_win != 0) {
            Win* w = slot_for(drag_win);
            if (w == nullptr || w->state == WinState::Minimized) {
                preview_clear();
                drag_zone = SnapZone::None;
                drag_win = 0;
                drag_engaged = false;
            } else {
                used = true;
                if (!drag_engaged) {
                    drag_engaged = true;
                    if (w->maximized || w->snap != SnapZone::None) {
                        // Dragging a maximized/snapped titlebar restores
                        // the floating frame re-anchored under the cursor.
                        const bool was_max = w->maximized;
                        const bool was_snap = w->snap != SnapZone::None;
                        Rect r = w->restore;
                        if (r.empty()) {
                            r = {x - cascade_w / 2,
                                 y - wm_chrome_height / 2, cascade_w,
                                 cascade_h};
                        }
                        hcpp::i32 relx = w->frame.w > 0
                                             ? (x - w->frame.x) * r.w /
                                                   w->frame.w
                                             : r.w / 2;
                        if (relx < 8) {
                            relx = 8;
                        }
                        if (relx > r.w - 8) {
                            relx = r.w - 8;
                        }
                        grab_off = {relx, grab_off.y};
                        w->maximized = false;
                        w->snap = SnapZone::None;
                        set_frame(*w, {x - relx, y - grab_off.y, r.w, r.h});
                        if (was_max) {
                            serial::write_line("win maximized=0");
                        } else if (was_snap) {
                            serial::write_line("snap zone=none");
                        }
                    }
                }
                const Rect next{x - grab_off.x, y - grab_off.y, w->frame.w,
                                w->frame.h};
                if (next != w->frame) {
                    set_frame(*w, next);
                }
                const SnapZone zone = zone_at(x, y);
                if (zone != drag_zone) {
                    preview_clear();
                    drag_zone = zone;
                    if (zone != SnapZone::None) {
                        preview = zone_rect(zone);
                        preview_on = true;
                        damage_add(preview);
                    }
                    serial::write("snap zone=");
                    serial::write_line(zone_name(zone));
                }
            }
        }
        // XAU mouse move: topmost window whose content region is under
        // the cursor (the hook cheap-rejects non-XAU windows).
        if (xau_event_hook != nullptr && drag_win == 0) {
            const Point p{x, y};
            hcpp::u32 order[wm_max_windows];
            const hcpp::u32 n = collect_order(order, false);
            for (hcpp::u32 k = 0; k < n; ++k) {
                Win& w = slots[order[k]];
                if (w.content_rect.contains(p)) {
                    xau_event_hook(w.id, wm_xau_ev_mousemove, p.x, p.y);
                    break;
                }
            }
        }
        if (drag_win == 0 && !wm_menu_visible() && !overlays.is_open()) {
            update_hover(x, y);
        } else if (hover_part != hot_none || hover_win != 0) {
            Win* old = slot_for(hover_win);
            if (old != nullptr) {
                damage_add(chrome_strip(*old));
            }
            hover_win = 0;
            hover_part = hot_none;
        }
        return used;
    }

    bool wm_drag_active() noexcept { return drag_win != 0; }

    bool wm_test_click(hcpp::i32 x, hcpp::i32 y) noexcept {
        return wm_handle_mouse(x, y, true);
    }

    bool wm_handle_key(char ascii) noexcept {
        if (overlays.is_open()) {
            const bool consumed = overlays.handle_key(ascii);
            if (!overlays.is_open()) {
                damage_full = true;
                menu_win = 0;
                ctx_active = false;
                ctx_count = 0;
            }
            return consumed;
        }
        return false;
    }

    hcpp::u32 wm_count() noexcept {
        hcpp::u32 n = 0;
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0) {
                ++n;
            }
        }
        return n;
    }

    const Win* wm_window(hcpp::u32 id) noexcept { return slot_for(id); }

    const Win* wm_window_at(hcpp::u32 index) noexcept {
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id == 0) {
                continue;
            }
            if (index == 0) {
                return &slots[i];
            }
            --index;
        }
        return nullptr;
    }

    const Win* wm_focused() noexcept {
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0 && slots[i].state == WinState::Focused) {
                return &slots[i];
            }
        }
        return nullptr;
    }

    bool wm_selftest() noexcept {
        const hcpp::u32 a = wm_open("HxAbout", Rect{});
        const hcpp::u32 b = wm_open("HxSettings", Rect{});
        const bool open_ok = a != 0 && b != 0;
        serial::write("wm wins=");
        serial::write_dec32(wm_count());
        serial::write("\r\n");
        bool z_ok = open_ok && wm_focus(a);
        const Win* wa = wm_window(a);
        const Win* wb = wm_window(b);
        z_ok = z_ok && wa != nullptr && wb != nullptr && wa->z > wb->z &&
               wa->state == WinState::Focused && wm_focused() == wa;
        bool close_ok = false;
        if (wa != nullptr) {
            const Rect btn = close_btn_rect(*wa);
            close_ok = wm_test_click(btn.x + btn.w / 2, btn.y + btn.h / 2) &&
                       wm_window(a) == nullptr;
        }
        serial::write("wm close=");
        serial::write_line(close_ok ? "ok" : "fail");
        serial::write("wm z=");
        serial::write_line(z_ok ? "ok" : "fail");
        bool menu_ok = wm_menu_open(b) && wm_menu_visible();
        if (menu_ok) {
            const Rect mr = menu_rect_now();
            const hcpp::i32 ix = mr.x + mr.w / 2;
            const hcpp::i32 iy = mr.y + menu_pad + 2 * menu_item_h +
                                 menu_item_h / 2;
            menu_ok = wm_test_click(ix, iy) && !wm_menu_visible() &&
                      wm_window(b) == nullptr;
        }
        serial::write("wm menu=");
        serial::write_line(menu_ok ? "ok" : "fail");
        bool snap_ok = false;
        const hcpp::u32 c = wm_open("SnapProbe", Rect{10, 10, 200, 120});
        const Win* wc = wm_window(c);
        if (wc != nullptr) {
            const Rect area = work_area();
            snap_ok = wm_snap(c, SnapZone::Left) &&
                      wc->frame ==
                          Rect{area.x, area.y, area.w / 2, area.h} &&
                      wc->snap == SnapZone::Left;
            snap_ok = wm_set_maximized(c, true) && snap_ok &&
                      wc->maximized && wc->frame == area;
            snap_ok = wm_set_maximized(c, false) && snap_ok &&
                      !wc->maximized && wc->snap == SnapZone::None &&
                      wc->frame == Rect{10, 10, 200, 120};
        }
        serial::write("wm snap=");
        serial::write_line(snap_ok ? "ok" : "fail");
        if (c != 0) {
            wm_close(c);
        }
        bool dirty_ok = wm_dirty();
        {
            static hcpp::u8 probe_mem[64 * 48 * 4];
            hcpp::graphics::Framebuffer probe_fb(probe_mem, 64, 48, 64 * 4, 32,
                                                 8, 16, 8, 8, 8, 0);
            wm_paint(probe_fb);
            dirty_ok = dirty_ok && !wm_dirty();
        }
        serial::write("wm dirty=");
        serial::write_line(dirty_ok ? "ok" : "fail");
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id != 0) {
                wm_close(slots[i].id);
            }
        }
        return open_ok && close_ok && z_ok && menu_ok && snap_ok &&
               dirty_ok && wm_count() == 0;
    }

    // --- XAU layer glue -------------------------------------------------

    void wm_set_xau_hooks(WmXauPaintHook paint,
                          WmXauEventHook event) noexcept {
        xau_paint_hook = paint;
        xau_event_hook = event;
    }

    bool wm_set_frame(hcpp::u32 id, hype::ui::Rect frame) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr || frame.empty()) {
            return false;
        }
        set_frame(*w, frame);
        return true;
    }

    void wm_set_caption(hcpp::u32 id, const char* caption) noexcept {
        Win* w = slot_for(id);
        if (w == nullptr || caption == nullptr) {
            return;
        }
        copy_name(w->caption, caption);
        damage_add(chrome_band(w->frame));
    }

}
