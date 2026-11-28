#include "kernel/win/winman.h"

#include "kernel/cfg/settings.h"
#include "kernel/font/hypefont.h"
#include "kernel/functionemitkeys.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/clipboard.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/screenshot.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/uiscale.h"
#include "kernel/ui/widget.h"
#include "kernel/ui/widgets/commandpalette.h"
#include "kernel/ui/widgets/contextmenu.h"
#include "kernel/win/deskicons.h"
#include "kernel/win/deskman.h"

namespace hype::win {

    // Base 32/40 px; ui_scale_init rewrites them with the DPI scale.
    hcpp::i32 wm_chrome_height = 32;
    hcpp::i32 wm_cascade_step = 40;

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Point;
        using hype::ui::Rect;
        using hype::ui::Size;
        using hype::ui::Theme;

        constexpr hcpp::i32 chrome_btn = 18;
        constexpr hcpp::i32 chrome_gap = 6;
        constexpr hcpp::i32 chrome_pad = 8;
        // Caption-button hit boxes: 22 px cells for the min/max/close
        // trio at the right of the title strip.
        constexpr hcpp::i32 ctl_btn_w = 22;
        constexpr hcpp::i32 ctl_btn_h = 24;
        // Aero Snap zones: edge proximity 16 px, corner boxes 32 px.
        constexpr hcpp::i32 snap_edge = 16;
        constexpr hcpp::i32 snap_corner = 32;
        constexpr hcpp::i32 menu_width = 140;
        constexpr hcpp::i32 menu_item_h = 22;
        constexpr hcpp::i32 menu_pad = 4;
        constexpr hcpp::u32 menu_item_count = 3;
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
        constexpr hcpp::u16 key_ctrl = 29;
        constexpr hcpp::u16 key_alt = 56;
        // Set-1 scancodes routed to the command palette while it owns
        // the keyboard (widgets wave-2): Backspace edits the query,
        // Up/Down move the selection, Enter commits.
        constexpr hcpp::u16 key_backspace = 14;
        constexpr hcpp::u16 key_enter = 28;
        constexpr hcpp::u16 key_k = 37;      // Ctrl+K opens the palette
        constexpr hcpp::u16 key_up = 72;
        constexpr hcpp::u16 key_down = 80;
        // Set-1 scancodes used by the clipboard chords (Ctrl/Win both
        // decode to these codes, incl. the E0-prefixed right variants).
        constexpr hcpp::u16 key_x = 45;
        constexpr hcpp::u16 key_c = 46;
        constexpr hcpp::u16 key_v = 47;
        constexpr hcpp::u16 key_lgui = 91;
        constexpr hcpp::u16 key_rgui = 92;
        // Set-1 scancodes for the virtual-desktop chords (issue #135):
        // Win+Ctrl+Left/Right switch, Win+Ctrl+D new, Win+Ctrl+F4/W
        // delete, Win+Shift+Left/Right move the focused window.
        constexpr hcpp::u16 key_w = 17;
        constexpr hcpp::u16 key_d = 32;
        constexpr hcpp::u16 key_f4 = 62;
        constexpr hcpp::u16 key_left = 75;
        constexpr hcpp::u16 key_right = 77;
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
        // Right-click menus ride the DropdownMenu-backed ContextMenu
        // widget (widgets wave-2); ctx_active marks the overlay slot as
        // owned by it.
        hype::ui::ContextMenu ctx_menu;
        bool ctx_active = false;
        // Command palette (Ctrl+K launcher) — shares the overlay slot.
        hype::ui::CommandPalette palette;
        bool palette_open = false;
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
        // Virtual desktops (issue #135): every window carries a `vdesk`
        // tag; only windows tagged with desk_current paint, hit-test,
        // enumerate for the taskbar and enter the Alt-Tab MRU list.
        // focus_mem remembers each desktop's focused window id so a
        // switch back restores exactly where the user left off.
        hcpp::u32 desk_count = vdesk_default;
        hcpp::u32 desk_current = 0;
        hcpp::u32 focus_mem[vdesk_max]{};

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
            return {strip.right() - ui::ui_px(chrome_pad) - ui::ui_px(ctl_btn_w),
                    strip.y + (strip.h - ui::ui_px(ctl_btn_h)) / 2, ui::ui_px(ctl_btn_w),
                    ui::ui_px(ctl_btn_h)};
        }

        Rect max_btn_rect(const Win& w) noexcept {
            const Rect close = close_btn_rect(w);
            return {close.x - ui::ui_px(ctl_btn_w), close.y, ui::ui_px(ctl_btn_w), ui::ui_px(ctl_btn_h)};
        }

        Rect min_btn_rect(const Win& w) noexcept {
            const Rect max = max_btn_rect(w);
            return {max.x - ui::ui_px(ctl_btn_w), max.y, ui::ui_px(ctl_btn_w), ui::ui_px(ctl_btn_h)};
        }

        Rect menu_btn_rect(const Win& w) noexcept {
            const Rect min = min_btn_rect(w);
            return {min.x - ui::ui_px(chrome_gap) - ui::ui_px(chrome_btn),
                    min.y + (min.h - ui::ui_px(chrome_btn)) / 2, ui::ui_px(chrome_btn), ui::ui_px(chrome_btn)};
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
            const bool near_l = x < area.x + ui::ui_px(snap_corner);
            const bool near_r = x >= area.right() - ui::ui_px(snap_corner);
            const bool near_t = y < area.y + ui::ui_px(snap_corner);
            const bool near_b = y >= area.bottom() - ui::ui_px(snap_corner);
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
            if (x < area.x + ui::ui_px(snap_edge)) {
                return SnapZone::Left;
            }
            if (x >= area.right() - ui::ui_px(snap_edge)) {
                return SnapZone::Right;
            }
            if (y < area.y + ui::ui_px(snap_edge)) {
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
            Rect r{menu_anchor.x, menu_anchor.y, ui::ui_px(menu_width),
                   ui::ui_px(menu_pad) * 2 +
                       static_cast<hcpp::i32>(menu_item_count) * ui::ui_px(menu_item_h)};
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
            const hcpp::i32 idx = (p.y - r.y - ui::ui_px(menu_pad)) / ui::ui_px(menu_item_h);
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
                                   box.y + ui::ui_px(menu_pad) +
                                       static_cast<hcpp::i32>(i) * ui::ui_px(menu_item_h),
                                   box.w - 2, ui::ui_px(menu_item_h)};
                    hcpp::i32 top = row.y + (row.h - line) / 2;
                    if (top < row.y) {
                        top = row.y;
                    }
                    paint_label(canvas, row.x + 10, top, menu_items[i],
                                theme.card_fg, 13);
                }
            }
        };

        WinMenu menu_widget;

        // Command palette action ids — app entries carry their
        // hxrt registry index, so action ids start above any real index.
        constexpr hcpp::u32 palette_act_base = 0x8000;
        constexpr hcpp::u32 palette_act_lock = palette_act_base + 0;
        constexpr hcpp::u32 palette_act_power = palette_act_base + 1;
        constexpr hcpp::u32 palette_act_desk = palette_act_base + 2;
        constexpr hcpp::u32 palette_act_shot = palette_act_base + 3;

        void palette_close() noexcept {
            if (!palette_open) {
                return;
            }
            palette_open = false;
            overlays.close();
            damage_full = true;
        }

        // Commits a palette pick: hxrt ids launch via desk_open_app,
        // action ids dispatch real shell calls.
        void palette_accept(hype::ui::CommandPalette&,
                            hcpp::u32 id, void*) noexcept {
            if (id < palette_act_base) {
                const hxrt::HxerHeader* const* list =
                    hxrt::hxrt_list();
                if (id < hxrt::hxrt_count() && list[id] != nullptr) {
                    serial::write("palette launch=");
                    serial::write_line(list[id]->name);
                    palette_close();
                    desk_open_app(list[id]->name);
                    return;
                }
            } else {
                serial::write("palette act=");
                serial::write_dec32(id - palette_act_base);
                serial::write("\r\n");
                palette_close();
                switch (id) {
                    case palette_act_lock:
                        hype::session::session_lock();
                        break;
                    case palette_act_power:
                        hype::session::session_power_dialog();
                        break;
                    case palette_act_desk:
                        vdesk_new();
                        break;
                    case palette_act_shot:
                        hype::ui::screenshot_request(false);
                        break;
                    default:
                        break;
                }
                return;
            }
            palette_close();
        }

        void palette_dismiss(hype::ui::CommandPalette&,
                             void*) noexcept {
            palette_close();
        }

        void palette_show() noexcept {
            if (palette_open) {
                palette_close();
                return;
            }
            palette.clear_items();
            const hxrt::HxerHeader* const* list = hxrt::hxrt_list();
            const hcpp::u32 apps = hxrt::hxrt_count();
            for (hcpp::u32 i = 0; i < apps; ++i) {
                if (list[i] != nullptr && !hxrt::hxrt_is_system(i)) {
                    palette.add_item({}, hcpp::StringView{list[i]->name},
                                     "App", i);
                }
            }
            palette.add_item(">", "Lock screen", "Action",
                             palette_act_lock);
            palette.add_item(">", "Shut down", "Action",
                             palette_act_power);
            palette.add_item(">", "New desktop", "Action",
                             palette_act_desk);
            palette.add_item(">", "Take screenshot", "Action",
                             palette_act_shot);
            palette.reset();
            palette.set_on_accept(&palette_accept, nullptr);
            palette.set_on_dismiss(&palette_dismiss, nullptr);
            // The palette shares the overlay slot: drop whatever held it.
            if (overlays.is_open()) {
                overlays.close();
            }
            menu_win = 0;
            ctx_active = false;
            ctx_menu.close();
            if (overlays.open(&palette)) {
                palette_open = true;
                damage_full = true;
                serial::write_line("palette open=1");
            }
        }

        // The DropdownMenu inside ctx_menu closes itself on commit /
        // outside-dismiss / Esc; when that happens the overlay slot must
        // be released too.
        void ctx_sync() noexcept {
            if (ctx_active && !ctx_menu.is_open()) {
                ctx_active = false;
                overlays.close();
                damage_full = true;
            }
        }

        // Caption-button kinds for paint_caption_btn.
        constexpr hcpp::u8 dot_min = 0;
        constexpr hcpp::u8 dot_max = 1;
        constexpr hcpp::u8 dot_close = 2;

        // HypeUI caption buttons (reskin v3): flat square cells at the
        // right of the strip — at rest they are transparent with a muted
        // glyph; on hover they fill (min/max -> muted, close ->
        // destructive) and the glyph flips to the foreground tone.
        void paint_caption_btn(Canvas& canvas, const Rect& cell,
                               const Theme& theme, hcpp::u8 kind,
                               bool focused, bool hover,
                               bool maximized) noexcept {
            const hcpp::i32 cx = cell.x + cell.w / 2;
            const hcpp::i32 cy = cell.y + cell.h / 2;
            const bool danger = kind == dot_close;
            if (hover) {
                canvas.fill_rounded_rect(
                    cell, static_cast<hcpp::i32>(theme.radius_xs),
                    canvas.color(danger ? theme.destructive : theme.muted));
            }
            const hcpp::u32 mark = canvas.color(
                hover ? (danger ? theme.accent_fg : theme.card_fg)
                      : (focused ? theme.muted_fg
                                 : hype::ui::rui_shade(theme.muted_fg, 0x60)));
            if (kind == dot_close) {
                canvas.draw_line(cx - 4, cy - 4, cx + 4, cy + 4, mark);
                canvas.draw_line(cx - 4, cy + 4, cx + 4, cy - 4, mark);
            } else if (kind == dot_max) {
                if (maximized) {
                    // Restore: two overlapping squares.
                    canvas.stroke_rect(Rect{cx - 1, cy - 4, 6, 6}, mark, 1);
                    canvas.stroke_rect(Rect{cx - 5, cy - 1, 6, 6}, mark, 1);
                } else {
                    canvas.stroke_rect(Rect{cx - 4, cy - 4, 9, 9}, mark, 1);
                }
            } else {
                canvas.fill_rect(Rect{cx - 5, cy + 2, 10, 2}, mark);
            }
        }

        void paint_chrome(Canvas& canvas, const Win& w) noexcept {
            const Theme& theme = hype::ui::theme_active();
            const Rect box = w.frame;
            if (box.empty()) {
                return;
            }
            const bool focused = w.state == WinState::Focused;
            hype::ui::rui_panel(canvas, box, hype::ui::rui_elev_window);
            const hcpp::i32 radius =
                static_cast<hcpp::i32>(theme.radius_lg);
            if (focused) {
                // Focused windows get the stronger outline over the
                // Type-3 2xl shadow (reskin v2 §5).
                canvas.stroke_rounded_rect(
                    box, radius, canvas.color(theme.border_strong));
            }
            const Rect strip = chrome_strip(w);
            if (strip.empty()) {
                return;
            }
            // HypeUI caption: flat `surface` band one tier below the
            // elevated window body, with a 1px hairline separator under
            // it (no gradient, no glow).
            hcpp::i32 band_h = strip.h + radius;
            const hcpp::i32 max_h = box.bottom() - 1 - strip.y;
            if (band_h > max_h) {
                band_h = max_h;
            }
            canvas.fill_rounded_rect(
                Rect{strip.x, strip.y, strip.w, band_h}, radius,
                canvas.color(theme.card));
            canvas.fill_rect(
                Rect{strip.x, strip.bottom() - 1, strip.w, 1},
                canvas.color(theme.border));

            const hcpp::i32 cy = strip.y + strip.h / 2;
            canvas.fill_circle(strip.x + ui::ui_px(chrome_pad) + 3, cy, 3,
                               canvas.color(theme.accent));

            const bool hot = hover_win == w.id;

            // Window-menu chip: ghost button — no border at rest, a
            // muted fill on hover; three bars for the glyph.
            const Rect menu_btn = menu_btn_rect(w);
            const bool menu_hot = hot && hover_part == hot_menu;
            if (menu_hot) {
                canvas.fill_rounded_rect(
                    menu_btn,
                    static_cast<hcpp::i32>(theme.radius_xs),
                    canvas.color(theme.muted));
            }
            const hcpp::u32 glyph =
                canvas.color(menu_hot ? theme.card_fg : theme.muted_fg);
            for (hcpp::i32 k = 0; k < 3; ++k) {
                canvas.fill_rect(
                    Rect{menu_btn.x + 4, menu_btn.y + 4 + k * 4,
                         menu_btn.w - 8, 2},
                    glyph);
            }

            // Caption buttons: flat min / max / close cells matching the
            // right-side hit rects. Unfocused windows get dimmer glyphs.
            paint_caption_btn(canvas, min_btn_rect(w), theme, dot_min,
                              focused, hot && hover_part == hot_min, false);
            paint_caption_btn(canvas, max_btn_rect(w), theme, dot_max,
                              focused, hot && hover_part == hot_max,
                              w.maximized);
            paint_caption_btn(canvas, close_btn_rect(w), theme, dot_close,
                              focused, hot && hover_part == hot_close,
                              false);

            const hcpp::i32 title_x = strip.x + ui::ui_px(chrome_pad) + 12;
            const hcpp::i32 max_text =
                menu_btn.x - ui::ui_px(chrome_gap) - title_x;
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
            // Long titles must clip at the caption buttons, not draw over
            // them — scope the canvas clip to the title budget rect.
            // Reskin v2: the caption always paints in the `dim` tone;
            // focus is carried by the border_strong outline instead.
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(
                Rect{title_x, strip.y, max_text, strip.h}));
            paint_label(canvas, title_x, top,
                        w.caption[0] != '\0' ? w.caption : w.app_name,
                        theme.muted_fg, 13);
            canvas.set_clip(saved_clip);
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

        // Z-order of the visible windows ON THE CURRENT DESKTOP: every
        // consumer (paint, hit tests, hover, XAU mouse delivery, MRU)
        // goes through this, so filtering here is what hides the other
        // desktops' windows (issue #135).
        hcpp::u32 collect_order(hcpp::u32* order, bool ascending) noexcept {
            hcpp::u32 n = 0;
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 && slots[i].vdesk == desk_current &&
                    slots[i].state != WinState::Minimized) {
                    order[n++] = i;
                }
            }
            sort_order(order, n, ascending);
            return n;
        }

        void set_frame(Win& w, const Rect& next) noexcept {
            // Damage covers the soft-shadow spill around the frame, not
            // just the frame itself — otherwise moving/snap leaves a
            // stale shadow ring on the desktop.
            const Rect prev = w.frame;
            damage_add(hype::ui::rui_paint_bounds(prev));
            w.frame = next;
            w.content_rect = content_for(next);
            damage_add(hype::ui::rui_paint_bounds(next));
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
            // Caption controls paint a soft shadow ~10 px below the strip —
            // use the full chrome band so hover transitions never leave
            // stale shadow edges inside the window body.
            Win* old = slot_for(hover_win);
            if (old != nullptr) {
                damage_add(chrome_band(old->frame));
            }
            hover_win = win;
            hover_part = part;
            Win* next = slot_for(hover_win);
            if (next != nullptr) {
                damage_add(chrome_band(next->frame));
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
                if (slots[i].id != 0 && slots[i].vdesk == desk_current &&
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
            hcpp::i32 w = ui::ui_px(sw_item_w);
            const hcpp::i32 need =
                ui::ui_px(sw_pad) * 2 + static_cast<hcpp::i32>(n) * w +
                (static_cast<hcpp::i32>(n) - 1) * ui::ui_px(sw_gap);
            if (need > max_w && n > 0) {
                w = (max_w - ui::ui_px(sw_pad) * 2 -
                     (static_cast<hcpp::i32>(n) - 1) * ui::ui_px(sw_gap)) /
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
                ui::ui_px(sw_pad) * 2 + static_cast<hcpp::i32>(n) * item_w +
                (static_cast<hcpp::i32>(n) - 1) * ui::ui_px(sw_gap);
            const hcpp::i32 h = ui::ui_px(sw_pad) * 2 + ui::ui_px(sw_item_h);
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
            // Panel shadow spills past switcher_rect — grow the damage so
            // opening/closing/moving selection never leaves a shadow ring.
            damage_add(hype::ui::rui_paint_bounds(switcher_rect()));
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
            // Panel shadow spills past switcher_rect — grow the damage so
            // opening/closing/moving selection never leaves a shadow ring.
            damage_add(hype::ui::rui_paint_bounds(switcher_rect()));
            alttab_marker();
        }

        void alttab_close(bool commit) noexcept {
            const hcpp::u32 id =
                switcher_open && switcher_sel < switcher_count
                    ? switcher_ids[switcher_sel]
                    : 0;
            if (switcher_open) {
                // Panel shadow spills past switcher_rect — grow the
                // damage so dismiss leaves no shadow ring.
                damage_add(hype::ui::rui_paint_bounds(switcher_rect()));
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
            damage_add(hype::ui::rui_paint_bounds(switcher_rect()));
        }

        // ---- virtual-desktop core (issue #135) -------------------------

        // Id of the Focused window anywhere in the slot table (the
        // wm keeps at most one), or 0. vdesk-agnostic on purpose: desk
        // transitions need the outgoing desktop's focus too.
        hcpp::u32 focused_id() noexcept {
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 &&
                    slots[i].state == WinState::Focused) {
                    return slots[i].id;
                }
            }
            return 0;
        }

        // Demote every Focused window to Normal (chrome repaint + XAU
        // focus-lost) — the same teardown wm_focus runs before raising.
        void clear_focus() noexcept {
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 &&
                    slots[i].state == WinState::Focused) {
                    damage_add(chrome_band(slots[i].frame));
                    slots[i].state = WinState::Normal;
                    if (xau_event_hook != nullptr) {
                        xau_event_hook(slots[i].id, wm_xau_ev_focus, 0, 0);
                    }
                }
            }
        }

        // Topmost non-minimized window on the current desktop, or 0.
        hcpp::u32 top_window_on_desk() noexcept {
            hcpp::u32 order[wm_max_windows];
            const hcpp::u32 n = collect_order(order, false);
            return n > 0 ? slots[order[0]].id : 0;
        }

        // Focus restore after desk_current changed (or tags renumbered):
        // prefer `prefer` when it lives on the current desktop, else the
        // remembered focus, else the topmost window; demote everything
        // when the desktop is empty.
        void desk_restore_focus(hcpp::u32 prefer) noexcept {
            hcpp::u32 want = prefer;
            const Win* w = slot_for(want);
            if (w == nullptr || w->vdesk != desk_current ||
                w->state == WinState::Minimized) {
                want = focus_mem[desk_current];
            }
            w = slot_for(want);
            if (w == nullptr || w->vdesk != desk_current ||
                w->state == WinState::Minimized) {
                want = top_window_on_desk();
            }
            if (want != 0) {
                wm_focus(want);  // demotes the outgoing focus + raises
            } else {
                clear_focus();
            }
        }

        // Core desktop transition: remember the outgoing desktop's focus,
        // tear down desktop-pinned UI state (switcher, menus, drag,
        // hover), restore the incoming desktop's focus and repaint.
        // Serial markers and the toast are left to callers.
        void desk_switch_apply(hcpp::u32 next) noexcept {
            if (switcher_open) {
                alttab_close(false);
            }
            if (palette_open) {
                palette_close();
            }
            if (wm_menu_visible()) {
                wm_menu_close();
            }
            if (wm_ctx_visible()) {
                wm_ctx_close();
            }
            // An in-flight titlebar drag or a hovered caption button is
            // pinned to a window of the outgoing desktop — drop it.
            preview_clear();
            drag_zone = SnapZone::None;
            drag_win = 0;
            drag_engaged = false;
            if (hover_win != 0) {
                Win* hw = slot_for(hover_win);
                if (hw != nullptr) {
                    damage_add(chrome_band(hw->frame));
                }
                hover_win = 0;
                hover_part = hot_none;
            }
            focus_mem[desk_current] = focused_id();
            desk_current = next;
            desk_restore_focus(0);
            damage_full = true;
        }

        // Serial marker "vdesk switch=<i>/<n>" plus the deskbar toast —
        // the on-screen affordance that names the landing desktop.
        void desk_report(hcpp::u32 index) noexcept {
            serial::write("vdesk switch=");
            serial::write_dec32(index + 1);
            serial::write_char('/');
            serial::write_dec32(desk_count);
            serial::write("\r\n");
            char msg[8];
            msg[0] = static_cast<char>('1' + index);
            msg[1] = ' ';
            msg[2] = '/';
            msg[3] = ' ';
            msg[4] = static_cast<char>('0' + desk_count);
            msg[5] = '\0';
            hype::ui::deskbar_notify("Desktop", msg);
        }

        // Persist the desktop count (issue #135 -> ui.vdesk_count, saved
        // through the debounced hype.cfg pipeline from issue #130).
        void persist_desk_count() noexcept {
            hype::cfg::settings_set_int("ui.vdesk_count",
                                        static_cast<hcpp::i32>(desk_count));
            hype::cfg::settings_request_save();
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
            // The open Start menu owns the keyboard: typed chars feed its
            // search field and Enter/Esc/arrows drive the results, so
            // chords like Alt-Tab must not run underneath (issue #129).
            if (hype::ui::deskbar_start_open()) {
                hype::ui::deskbar_handle_key(
                    e.code,
                    pressed ? input::scancode_last_ascii() : '\0');
                return;
            }
            // The open command palette owns the keyboard too (widgets
            // wave-2): scancodes map to the widget's key alphabet —
            // printable chars feed the query, Backspace edits,
            // Up/Down move the selection, Enter commits, Esc dismisses.
            if (palette_open) {
                if (!pressed) {
                    return;
                }
                char key = '\0';
                switch (e.code) {
                    case key_escape: key = 27; break;
                    case key_backspace: key = '\b'; break;
                    case key_enter: key = '\n'; break;
                    case key_up: key = hype::ui::keys::up; break;
                    case key_down: key = hype::ui::keys::down; break;
                    default:
                        key = input::scancode_last_ascii();
                        break;
                }
                overlays.handle_key(key);
                if (!overlays.is_open()) {
                    palette_open = false;
                }
                damage_full = true;
                return;
            }
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
                return;
            }
            if (e.code == key_escape && switcher_open) {
                alttab_close(false);
                return;
            }
            if (e.code == input::key_prtsc) {
                // Screenshots (issue #131): PrtSc grabs the full desktop,
                // Ctrl/Alt+PrtSc the focused window. The request only arms
                // a flag — screenshot_pump() runs the encode+memfs write
                // on the next desktop-loop pass, off the IRQ path.
                hype::ui::screenshot_request(
                    input::scancode_key_pressed(key_ctrl) ||
                    input::scancode_key_pressed(key_alt));
                return;
            }
            const bool ctrl = input::scancode_key_pressed(key_ctrl);
            const bool gui = input::scancode_key_pressed(key_lgui) ||
                             input::scancode_key_pressed(key_rgui);
            // Win+V toggles the clipboard-history flyout (deskbar).
            if (gui && e.code == key_v) {
                hype::ui::deskbar_toggle_clip();
                return;
            }
            // Virtual desktops (issue #135): Win+Ctrl+Left/Right switch
            // desktops with wraparound, Win+Ctrl+D appends one,
            // Win+Ctrl+F4 (or W) deletes the current one and
            // Win+Shift+Left/Right moves the focused window to a
            // neighboring desktop.
            if (gui && ctrl) {
                if (e.code == key_left || e.code == key_right) {
                    vdesk_step(e.code == key_right ? 1 : -1);
                    return;
                }
                if (e.code == key_d) {
                    vdesk_new();
                    return;
                }
                if (e.code == key_f4 || e.code == key_w) {
                    vdesk_delete();
                    return;
                }
            }
            if (gui && !ctrl && input::scancode_shift_active() &&
                (e.code == key_left || e.code == key_right)) {
                vdesk_move_focused(e.code == key_right ? 1 : -1);
                return;
            }
            // While the flyout is up it takes Esc and the number-row
            // picks; every other key keeps flowing to the focused app.
            if (hype::ui::deskbar_clip_key(e.code)) {
                return;
            }
            // While the calendar flyout is up it takes Esc and the
            // Left/Right month nav; every other key flows to the app.
            if (hype::ui::deskbar_cal_key(e.code)) {
                return;
            }
            // Ctrl+K toggles the command palette (widgets wave-2) —
            // the keyboard launcher for apps and shell actions.
            if (ctrl && !gui && e.code == key_k) {
                palette_show();
                return;
            }
            // Ctrl+C/X/V dispatch: the focused XAU window's WndProc
            // answers via XAU_MSG_COPY/CUT/PASTE (apps own what the
            // selection is; the wm only routes the chord).
            if (ctrl && !gui) {
                if (e.code == key_c) {
                    hype::ui::clip_copy();
                } else if (e.code == key_x) {
                    hype::ui::clip_cut();
                } else if (e.code == key_v) {
                    hype::ui::clip_paste();
                }
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
                    panel.x + ui::ui_px(sw_pad) +
                        static_cast<hcpp::i32>(i) * (item_w + ui::ui_px(sw_gap)),
                    panel.y + ui::ui_px(sw_pad), item_w, ui::ui_px(sw_item_h)};
                const bool sel = i == switcher_sel;
                hype::ui::rui_control_bg(
                    canvas, item,
                    sel ? hype::ui::rui_lift(theme.secondary, 2)
                        : theme.secondary,
                    sel ? theme.accent : theme.border);
                if (sel) {
                    canvas.fill_rect_alpha(
                        item.inset(hype::ui::Insets::uniform(1)),
                        canvas.color(theme.accent_soft),
                        theme.accent_soft_alpha);
                }
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
                     ui::ui_px(cascade_w), ui::ui_px(cascade_h)};
            ++cascade_index;
        }
        Win& w = slots[slot];
        w = Win{};
        w.id = slot + 1;
        w.vdesk = static_cast<hcpp::u8>(desk_current);
        copy_name(w.app_name, app_name);
        w.frame = frame;
        w.content_rect = content_for(frame);
        w.state = WinState::Normal;
        w.z = ++z_counter;
        w.ctx = hxrt::AppCtx{};
        w.ctx.region = w.content_rect;
        wm_focus(w.id);
        damage_add(hype::ui::rui_paint_bounds(frame));
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
        // Forget any per-desktop focus memory pointing at this slot.
        for (hcpp::u32 i = 0; i < vdesk_max; ++i) {
            if (focus_mem[i] == id) {
                focus_mem[i] = 0;
            }
        }
        damage_add(hype::ui::rui_paint_bounds(gone));
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
        // Cross-desktop activation (issue #135): focusing a window that
        // lives on another desktop switches to it first — same outcome
        // as clicking its task button over there (e.g. reopening an
        // already-running app from the Start menu).
        if (w->vdesk != desk_current) {
            desk_switch_apply(w->vdesk);
            desk_report(w->vdesk);
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
                damage_add(hype::ui::rui_paint_bounds(w->frame));
            }
            w->state = WinState::Focused;
            // Raising z makes this window paint over any sibling it
            // overlaps — the whole frame (plus shadow spill) must be
            // damaged, not just the chrome band, or stale pixels of the
            // previously topmost window survive in the overlap area.
            damage_add(hype::ui::rui_paint_bounds(w->frame));
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
            damage_add(hype::ui::rui_paint_bounds(w->frame));
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
            damage_add(hype::ui::rui_paint_bounds(w->frame));
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
            // A window's shadow spills past w.frame — repaint it whenever
            // the spill region touches the clip, not only the frame.
            if (!hype::ui::rui_paint_bounds(w.frame).intersects(clip)) {
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
        }
        // Translucent snap-preview hint while a titlebar drag hovers a
        // zone (reskin v2: accent_soft wash + r8 outline).
        if (preview_on && !preview.empty()) {
            canvas.fill_rect_alpha(preview,
                                   canvas.color(theme.accent_soft),
                                   theme.accent_soft_alpha);
            canvas.stroke_rounded_rect(
                preview, static_cast<hcpp::i32>(theme.radius_md),
                canvas.color(theme.accent));
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
                damage_add(hype::ui::rui_paint_bounds(slots[i].frame));
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
        ctx_menu.close();
        palette_open = false;
        menu_widget.set_preferred_size(
            Size{ui::ui_px(menu_width),
                 ui::ui_px(menu_pad) * 2 +
                     static_cast<hcpp::i32>(menu_item_count) * ui::ui_px(menu_item_h)});
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
        ctx_menu.close();
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
        // Every shell menu shares the single overlay slot: drop whatever
        // holds it now (window menu, context menu, command palette).
        if (overlays.is_open()) {
            overlays.close();
        }
        menu_win = 0;
        palette_open = false;
        // WmCtxItem -> ContextMenu::Item translation; the flag bits are
        // identical (separator=1, disabled=2) so values pass through.
        hype::ui::ContextMenu::Item rows[wm_ctx_max_items];
        for (hcpp::u32 i = 0; i < count; ++i) {
            rows[i] = {items[i].label, items[i].id, items[i].flags};
        }
        ctx_menu.set_screen(menu_screen);
        if (!ctx_menu.open_at(Point{x, y}, rows, count, pick, user)) {
            return false;
        }
        if (!overlays.open(&ctx_menu)) {
            ctx_menu.close();
            return false;
        }
        ctx_active = true;
        damage_full = true;
        serial::write("ctx menu=open items=");
        serial::write_dec32(count);
        serial::write("\r\n");
        return true;
    }

    void wm_ctx_close() noexcept {
        if (!ctx_active) {
            return;
        }
        damage_full = true;
        ctx_menu.close();
        overlays.close();
        ctx_active = false;
    }

    bool wm_ctx_visible() noexcept {
        return ctx_active && overlays.is_open();
    }

    bool wm_overlay_open() noexcept { return overlays.is_open(); }

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
        if (palette_open) {
            // Press-to-commit parity with the menus: Down arms a row,
            // Up commits it; a press outside asks the widget to dismiss.
            palette.handle_mouse(p, hype::ui::PaletteMouse::Down);
            if (palette_open) {
                palette.handle_mouse(p, hype::ui::PaletteMouse::Up);
            }
            damage_full = true;
            return true;
        }
        if (wm_ctx_visible()) {
            // Same press-to-commit parity the bespoke ctx menu had.
            ctx_menu.handle_mouse(p, hype::ui::MenuMouse::Down);
            if (ctx_menu.is_open()) {
                ctx_menu.handle_mouse(p, hype::ui::MenuMouse::Up);
            }
            ctx_sync();
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
                    damage_add(hype::ui::rui_paint_bounds(w.frame));
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
        if (palette_open) {
            palette_close();
            return true;
        }
        if (wm_ctx_visible()) {
            wm_ctx_close();
            return true;
        }
        if (wm_menu_visible()) {
            wm_menu_close();
            return true;
        }
        // Open shell flyouts (notification center) get right-click first:
        // a press on a history row dismisses it, a press outside closes
        // the flyout and falls through to normal routing.
        if (hype::ui::deskbar_handle_mouse_r(x, y)) {
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
        // Hover tracking for the open menus: moves the highlight (full
        // repaint is already guaranteed while an overlay is up).
        if (palette_open) {
            palette.handle_mouse({x, y}, hype::ui::PaletteMouse::Move);
            used = true;
        } else if (wm_ctx_visible()) {
            ctx_menu.handle_mouse({x, y}, hype::ui::MenuMouse::Move);
            used = true;
        }
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
                            r = {x - ui::ui_px(cascade_w) / 2,
                                 y - wm_chrome_height / 2, ui::ui_px(cascade_w),
                                 ui::ui_px(cascade_h)};
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
        // Deskbar chip hot-tracking (reskin v2): the bar paints
        // accent_soft hover washes for Start/task/tray zones.
        hype::ui::deskbar_handle_move(x, y);
        if (drag_win == 0 && !wm_menu_visible() && !overlays.is_open()) {
            update_hover(x, y);
        } else if (hover_part != hot_none || hover_win != 0) {
            Win* old = slot_for(hover_win);
            if (old != nullptr) {
                damage_add(chrome_band(old->frame));
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
        // Character-only callers still reach the open Start menu's
        // search field (scancode-less path, issue #129).
        if (hype::ui::deskbar_start_open()) {
            return hype::ui::deskbar_handle_key(0, ascii);
        }
        if (overlays.is_open()) {
            const bool consumed = overlays.handle_key(ascii);
            if (!overlays.is_open()) {
                damage_full = true;
                menu_win = 0;
                palette_open = false;
                ctx_active = false;
                ctx_menu.close();
            } else {
                ctx_sync();
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
        // Enumerates only the current desktop's windows (issue #135) —
        // this is what filters the deskbar task buttons per desktop.
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id == 0 || slots[i].vdesk != desk_current) {
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
            if (slots[i].id != 0 && slots[i].vdesk == desk_current &&
                slots[i].state == WinState::Focused) {
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
            const hcpp::i32 iy = mr.y + ui::ui_px(menu_pad) + 2 * ui::ui_px(menu_item_h) +
                                 ui::ui_px(menu_item_h) / 2;
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

    // --- Virtual desktops (issue #135) ------------------------------------

    hcpp::u32 vdesk_count() noexcept { return desk_count; }

    hcpp::u32 vdesk_current() noexcept { return desk_current; }

    bool vdesk_switch(hcpp::u32 index) noexcept {
        if (index >= desk_count) {
            return false;
        }
        if (index == desk_current) {
            return true;
        }
        desk_switch_apply(index);
        desk_report(index);
        return true;
    }

    bool vdesk_step(hcpp::i32 dir) noexcept {
        const hcpp::u32 next = static_cast<hcpp::u32>(
            (static_cast<hcpp::i32>(desk_current) + dir +
             static_cast<hcpp::i32>(desk_count)) %
            static_cast<hcpp::i32>(desk_count));
        return vdesk_switch(next);
    }

    bool vdesk_new() noexcept {
        if (desk_count >= vdesk_max) {
            return false;
        }
        ++desk_count;
        serial::write("vdesk new=");
        serial::write_dec32(desk_count);
        serial::write("\r\n");
        persist_desk_count();
        vdesk_switch(desk_count - 1);
        return true;
    }

    bool vdesk_delete() noexcept {
        if (desk_count <= vdesk_min) {
            return false;
        }
        const hcpp::u32 gone = desk_current;
        // The deleted desktop's focused window follows its windows to the
        // surviving desktop and stays focused there.
        const hcpp::u32 carried = focused_id();
        // Windows on the deleted desktop move to the previous one (or
        // absorb into the renumbered desk 0 when desktop 0 goes away —
        // that's the "next" desktop after the shift); higher tags
        // renumber down.
        const hcpp::u32 into = gone > 0 ? gone - 1 : 0;
        for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
            if (slots[i].id == 0) {
                continue;
            }
            if (slots[i].vdesk == gone) {
                slots[i].vdesk = static_cast<hcpp::u8>(into);
            } else if (slots[i].vdesk > gone) {
                --slots[i].vdesk;
            }
        }
        // Per-desktop focus memory shifts down over the deleted slot.
        for (hcpp::u32 i = gone; i + 1 < desk_count; ++i) {
            focus_mem[i] = focus_mem[i + 1];
        }
        focus_mem[desk_count - 1] = 0;
        --desk_count;
        serial::write("vdesk del=");
        serial::write_dec32(desk_count);
        serial::write("\r\n");
        persist_desk_count();
        if (switcher_open) {
            alttab_close(false);
        }
        if (palette_open) {
            palette_close();
        }
        if (wm_menu_visible()) {
            wm_menu_close();
        }
        if (wm_ctx_visible()) {
            wm_ctx_close();
        }
        desk_current = gone > 0 ? gone - 1 : 0;
        desk_restore_focus(carried);
        damage_full = true;
        desk_report(desk_current);
        return true;
    }

    bool vdesk_move_focused(hcpp::i32 dir) noexcept {
        Win* w = slot_for(focused_id());
        if (w == nullptr) {
            return false;
        }
        const hcpp::i32 target =
            static_cast<hcpp::i32>(desk_current) + dir;
        if (target < 0 || target >= static_cast<hcpp::i32>(desk_count)) {
            return false;
        }
        w->vdesk = static_cast<hcpp::u8>(target);
        // Remember it as the target desktop's focus so switching there
        // lands on the moved window; the current desktop hands focus to
        // its next topmost survivor.
        focus_mem[target] = w->id;
        focus_mem[desk_current] = 0;
        desk_restore_focus(0);
        damage_full = true;
        serial::write("vdesk move=");
        serial::write_dec32(static_cast<hcpp::u32>(target) + 1);
        serial::write("\r\n");
        return true;
    }

    void vdesk_set_count(hcpp::u32 count) noexcept {
        if (count < vdesk_min) {
            count = vdesk_min;
        }
        if (count > vdesk_max) {
            count = vdesk_max;
        }
        if (count == desk_count) {
            return;
        }
        if (count < desk_count) {
            // Windows on dropped desktops merge into the last survivor.
            for (hcpp::u32 i = 0; i < wm_max_windows; ++i) {
                if (slots[i].id != 0 &&
                    slots[i].vdesk >= count) {
                    slots[i].vdesk = static_cast<hcpp::u8>(count - 1);
                }
            }
            for (hcpp::u32 i = count; i < desk_count; ++i) {
                focus_mem[i] = 0;
            }
            if (desk_current >= count) {
                desk_current = count - 1;
            }
        }
        desk_count = count;
        desk_restore_focus(0);
        damage_full = true;
    }

}
