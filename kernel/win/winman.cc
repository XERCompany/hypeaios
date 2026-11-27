#include "kernel/win/winman.h"

#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget.h"

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
        constexpr hcpp::i32 menu_width = 140;
        constexpr hcpp::i32 menu_item_h = 22;
        constexpr hcpp::i32 menu_pad = 4;
        constexpr hcpp::u32 menu_item_count = 3;
        constexpr hcpp::i32 cascade_w = 520;
        constexpr hcpp::i32 cascade_h = 340;
        constexpr hcpp::u32 cascade_wrap = 6;

        const char* const menu_items[menu_item_count] = {"Move", "Minimize",
                                                         "Close"};

        Win slots[wm_max_windows];
        hcpp::u32 z_counter = 0;
        hcpp::u32 cascade_index = 0;
        hype::ui::OverlayHost overlays;
        hcpp::u32 menu_win = 0;
        Point menu_anchor{};
        Rect menu_screen{0, 0, 1024, 768};
        Rect damage{};
        bool damaged = false;
        bool damage_full = false;

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
            return {strip.right() - chrome_pad - chrome_btn,
                    strip.y + (strip.h - chrome_btn) / 2, chrome_btn,
                    chrome_btn};
        }

        Rect menu_btn_rect(const Win& w) noexcept {
            const Rect close = close_btn_rect(w);
            return {close.x - chrome_gap - chrome_btn, close.y, chrome_btn,
                    chrome_btn};
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
                    canvas.fill_rect(row, canvas.color(theme.card));
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

            const Rect menu_btn = menu_btn_rect(w);
            hype::ui::rui_control_bg(canvas, menu_btn, theme.card,
                                     theme.border);
            for (hcpp::i32 k = 0; k < 3; ++k) {
                canvas.fill_rect(
                    Rect{menu_btn.x + 4, menu_btn.y + 4 + k * 4,
                         menu_btn.w - 8, 2},
                    canvas.color(theme.card_fg));
            }

            const Rect close_btn = close_btn_rect(w);
            hype::ui::rui_control_bg(canvas, close_btn,
                                     hype::ui::rui_shade(theme.destructive,
                                                         40),
                                     theme.destructive);
            canvas.draw_line(close_btn.x + 5, close_btn.y + 5,
                             close_btn.right() - 6, close_btn.bottom() - 6,
                             canvas.color(theme.primary_fg));
            canvas.draw_line(close_btn.x + 5, close_btn.bottom() - 6,
                             close_btn.right() - 6, close_btn.y + 5,
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
            paint_label(canvas, title_x, top, w.app_name,
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
                if (slots[i].id != 0) {
                    order[n++] = i;
                }
            }
            sort_order(order, n, ascending);
            return n;
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
        const bool was_focused = w->state == WinState::Focused;
        if (menu_win == id) {
            wm_menu_close();
        }
        const Rect gone = w->frame;
        *w = Win{};
        damage_add(gone);
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
            }
        }
        if (w->state != WinState::Focused) {
            w->state = WinState::Focused;
            damage_add(chrome_band(w->frame));
        }
        w->z = ++z_counter;
        return true;
    }

    void wm_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return;
        }
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
            if (!content_clip.empty() &&
                !hxrt::hxrt_exec(w.app_name, w.ctx)) {
                canvas.fill_rect(region, canvas.color(theme.card));
                paint_label(canvas, region.x + 16, region.y + 16,
                            "app unavailable", theme.muted_fg, 13);
            }
        }
        overlays.paint_overlays(canvas, screen);
    }

    void wm_invalidate(hype::ui::Rect region) noexcept {
        damage_add(region);
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
    }

    bool wm_menu_visible() noexcept {
        return menu_win != 0 && overlays.is_open();
    }

    bool wm_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept {
        if (!pressed) {
            return false;
        }
        const Point p{x, y};
        if (wm_menu_visible()) {
            const hcpp::u32 target = menu_win;
            const hcpp::i32 item = menu_item_at(p);
            if (item == 2) {
                wm_close(target);
            } else {
                if (item == 0) {
                    serial::write_line("wm move=stub");
                } else if (item == 1) {
                    serial::write_line("wm minimize=stub");
                }
                wm_menu_close();
            }
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
            if (menu_btn_rect(w).contains(p)) {
                return wm_menu_open(w.id);
            }
            return wm_focus(w.id);
        }
        return false;
    }

    bool wm_test_click(hcpp::i32 x, hcpp::i32 y) noexcept {
        return wm_handle_mouse(x, y, true);
    }

    bool wm_handle_key(char ascii) noexcept {
        if (overlays.is_open()) {
            const bool consumed = overlays.handle_key(ascii);
            if (!overlays.is_open()) {
                damage_full = true;
                menu_win = 0;
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
        return open_ok && close_ok && z_ok && menu_ok && dirty_ok &&
               wm_count() == 0;
    }

}
