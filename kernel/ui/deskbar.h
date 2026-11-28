#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    // Bottom taskbar strip: Start button + menu, per-window task buttons,
    // system tray clock. Rendered by desk_paint on top of the window layer.
    constexpr hcpp::i32 deskbar_height = 36;

    // Idle hook: watches the window snapshot and the RTC minute and marks
    // only the affected bar cells dirty through winman damage tracking.
    void deskbar_idle() noexcept;

    // Paints the bar (and the open Start menu) inside `clip`, the damage
    // rect desk_paint already computed for this frame.
    void deskbar_paint(hcpp::graphics::Framebuffer& fb, Rect clip) noexcept;

    // Routes a primary-button click through the taskbar hit zones. Returns
    // true when the click was consumed (bar area or open Start menu).
    bool deskbar_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept;

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

}
