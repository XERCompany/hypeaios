#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    // Bottom taskbar strip: Start button + menu, per-window task buttons,
    // system tray clock. Rendered by desk_paint on top of the window layer.
    // DPI-aware (issue #174): holds the scaled value after ui_scale_init,
    // 36 px at 1.00x.
    extern hcpp::i32 deskbar_height;

    // Idle hook: watches the window snapshot and the RTC minute and marks
    // only the affected bar cells dirty through winman damage tracking.
    void deskbar_idle() noexcept;

    // Paints the bar (and the open Start menu) inside `clip`, the damage
    // rect desk_paint already computed for this frame.
    void deskbar_paint(hcpp::graphics::Framebuffer& fb, Rect clip) noexcept;

    // Routes a primary-button click through the taskbar hit zones. Returns
    // true when the click was consumed (bar area or open Start menu).
    bool deskbar_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept;

    // Key routing while the Start menu is open (issue #129): printable
    // `ascii` feeds the auto-focused search field, Backspace edits,
    // Up/Down move the result selection, Enter activates it and Esc
    // clears the query (a second Esc closes the menu). `code` is the
    // set-1 scancode; pass 0 when only the character is known. Returns
    // true while the menu is open — it owns the keyboard.
    bool deskbar_handle_key(hcpp::u16 code, char ascii) noexcept;

    bool deskbar_start_open() noexcept;
    void deskbar_open_start() noexcept;
    void deskbar_close_start() noexcept;

    // Shell notification entry point: copies `title`/`msg` into tray-owned
    // storage, pushes a ToastHost card (bottom-right, above the taskbar),
    // archives a copy in the persistent notification-center history ring
    // (last 24), bumps the bell's unread badge and prints the serial
    // markers "toast push=<title>" / "tray unread=N" / "notif hist=N".
    void deskbar_notify(const char* title, const char* msg) noexcept;

    // Quick-settings flyout state (speaker/net tray glyph toggles it).
    bool deskbar_qs_open() noexcept;
    void deskbar_close_qs() noexcept;

    // Quiet slider-state sync for the settings layer (issue #130): sets
    // the stored 0..100 value and repaints the row when QS is open,
    // without the serial marker, mixer write or settings re-save the
    // user-gesture path (apply_volume/apply_bright) performs.
    void deskbar_set_volume(hcpp::i32 pct) noexcept;
    void deskbar_set_brightness(hcpp::i32 pct) noexcept;

    // Notification-center flyout state (tray bell toggles it). Opening
    // marks the archive read (unread -> 0).
    bool deskbar_nc_open() noexcept;
    void deskbar_close_nc() noexcept;

    // Right-click hook for the open notification center: dismisses the
    // history row under the cursor; a press outside closes the flyout and
    // falls through to normal right-click routing. Returns true when the
    // press was consumed by the panel.
    bool deskbar_handle_mouse_r(hcpp::i32 x, hcpp::i32 y) noexcept;

    // Clipboard-history flyout (Win+V): a deskbar panel over the
    // hype::ui clip store in kernel/ui/clipboard.cc. Rows are numbered
    // previews; click or a number-row key pastes that entry into the
    // focused window, Esc or an outside click closes.
    bool deskbar_clip_open() noexcept;
    void deskbar_open_clip() noexcept;
    void deskbar_close_clip() noexcept;
    void deskbar_toggle_clip() noexcept;
    // Scancode handler from winman's key hook: Esc closes, number-row
    // codes 2..10 ("1".."9") paste history rows 0..8. Returns true only
    // when the flyout is open and consumed the key.
    bool deskbar_clip_key(hcpp::u16 code) noexcept;

    // Calendar + clock flyout (issue #132): the tray-clock cell toggles a
    // bottom-right panel with a big HH:MM clock, the full date line and a
    // navigable month grid (prev/next arrows, Monday-first columns,
    // today accent-marked, ±120-month bound). An outside click or Esc
    // closes it; prints "cal open=ok" / "cal nav=<y-m>".
    bool deskbar_cal_open() noexcept;
    void deskbar_close_cal() noexcept;
    // Scancode handler from winman's key hook: Esc closes, Left/Right
    // step the viewed month. Returns true only when the flyout is open
    // and consumed the key.
    bool deskbar_cal_key(hcpp::u16 code) noexcept;

}
