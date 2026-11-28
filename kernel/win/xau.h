#pragma once

// XAU — HypeAI OS windowing API.
//
// A Windows-style windowing layer built on top of the window manager
// (kernel/win/winman.cc): opaque window handles, WndProc-style message
// dispatch, a small class registry, and a posted-message queue. XAU
// handles map onto wm window slots (index + generation packed into a
// u32); every XAU window is a regular wm window underneath, so chrome,
// focus, drag, snap and the deskbar keep working unchanged.
//
// Two ways to get an XAU window:
//   1. xau_create(class_name, title, ...)  — explicit CreateWindow-like
//      path; the wm slot gets a synthetic unique app name and the title
//      is applied through the wm caption.
//   2. xau_register_class("AppName", proc, ...) + an existing wm_open of
//      that app name — the first paint/input event adopts the wm window
//      into XAU automatically (this is how the ported .hxer apps work).

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::win {

    // Opaque window handle: (generation << 8) | wm_window_id.
    using XAU = hcpp::u32;
    constexpr XAU XAU_INVALID = 0;

    // WndProc signature. Return convention matches Windows: message-
    // specific, 0 when handled; unhandled messages should be forwarded
    // to xau_defproc.
    using XauWndProc = hcpp::i32 (*)(XAU xau_win, hcpp::u32 msg,
                                   hcpp::u64 wparam,
                                   hcpp::u64 lparam) noexcept;

    // Message ids (XAU_MSG_*), mirroring the classic Win32 names.
    enum XauMsg : hcpp::u32 {
        XAU_MSG_CREATE = 1,      // window created; lparam = 0
        XAU_MSG_DESTROY = 2,     // window is being destroyed
        XAU_MSG_PAINT = 3,       // lparam = const XauDC* draw context
        XAU_MSG_CLOSE = 4,       // close requested (defproc destroys)
        XAU_MSG_SIZE = 5,        // lparam = (h<<16)|w of the frame
        XAU_MSG_MOVE = 6,        // lparam = (y<<16)|x of the frame
        XAU_MSG_LBUTTONDOWN = 7, // lparam = client coords (y<<16)|x
        XAU_MSG_LBUTTONUP = 8,
        XAU_MSG_RBUTTONDOWN = 9,
        XAU_MSG_RBUTTONUP = 10,
        XAU_MSG_MOUSEMOVE = 11,  // lparam = client coords
        XAU_MSG_KEYDOWN = 12,    // wparam = scancode, lparam = ascii
        XAU_MSG_KEYUP = 13,
        XAU_MSG_CHAR = 14,       // wparam = ascii character
        XAU_MSG_MINIMIZE = 15,   // wparam = 1 minimized, 0 restored
        XAU_MSG_MAXIMIZE = 16,   // wparam = 1 maximized, 0 restored
        XAU_MSG_FOCUS = 17,      // wparam = 1 gained, 0 lost
        XAU_MSG_TIMER = 18,      // wparam = timer id
        // Clipboard messages (kernel/ui/clipboard.cc): delivered to the
        // focused window by the Ctrl+C/X/V chord dispatch. Apps pull or
        // push text via hype::ui::clip_set/clip_get — nothing rides in
        // wparam/lparam.
        XAU_MSG_COPY = 19,       // app should clip_set() its selection
        XAU_MSG_CUT = 20,        // copy + remove the selection
        XAU_MSG_PASTE = 21,      // app should insert clip_get() text
    };

    // xau_create style flags (XAU_WS_*).
    constexpr hcpp::u32 XAU_WS_VISIBLE = 1u;
    constexpr hcpp::u32 XAU_WS_MINIMIZED = 2u;
    constexpr hcpp::u32 XAU_WS_MAXIMIZED = 4u;

    // Draw context handed to XAU_MSG_PAINT via lparam. All rects are in
    // screen coordinates; `client` is the window's content region.
    struct XauDC {
        hcpp::graphics::Framebuffer* fb = nullptr;
        hype::ui::Rect client{};
        hype::ui::Rect clip{};
    };

    constexpr hcpp::u32 xau_max_classes = 16;
    constexpr hcpp::u32 xau_post_capacity = 64;
    constexpr hcpp::u32 xau_max_timers = 8;

    // Registers a window class (16 slots). `bg` is a packed 0xRRGGBB
    // client fill for xau_defproc's default paint; pass 0 to use the
    // active theme card color.
    bool xau_register_class(const char* name, XauWndProc proc,
                            hcpp::u32 style, hcpp::u32 bg) noexcept;

    // CreateWindow-like entry point. `class_name` selects a registered
    // class when `proc` is null; an explicit `proc` wins over the class.
    // Zero/negative w/h lets the wm cascade the window.
    XAU xau_create(const char* class_name, const char* title, hcpp::i32 x,
                   hcpp::i32 y, hcpp::i32 w, hcpp::i32 h, hcpp::u32 flags,
                   XauWndProc proc) noexcept;

    bool xau_destroy(XAU xau_win) noexcept;
    bool xau_show(XAU xau_win) noexcept;
    bool xau_hide(XAU xau_win) noexcept;
    bool xau_move(XAU xau_win, hcpp::i32 x, hcpp::i32 y) noexcept;
    bool xau_get_rect(XAU xau_win, hype::ui::Rect& out) noexcept;
    bool xau_set_rect(XAU xau_win, const hype::ui::Rect& frame) noexcept;
    // Client-space invalidation; nullptr rect invalidates the whole
    // window frame.
    bool xau_invalidate(XAU xau_win, const hype::ui::Rect* rect) noexcept;
    bool xau_set_title(XAU xau_win, const char* title) noexcept;
    bool xau_focus(XAU xau_win) noexcept;

    // Returns the client (content) rect in screen coordinates.
    bool xau_client_rect(XAU xau_win, hype::ui::Rect& out) noexcept;
    bool xau_valid(XAU xau_win) noexcept;
    XAU xau_focused() noexcept;

    // Send = synchronous dispatch to the WndProc. Post = enqueue into a
    // 64-entry FIFO drained by xau_pump (wired into the desktop frame
    // loop).
    hcpp::i32 xau_send(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                       hcpp::u64 lparam) noexcept;
    bool xau_post(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                  hcpp::u64 lparam) noexcept;
    void xau_pump() noexcept;

    // Periodic XAU_MSG_TIMER delivery; `ticks` is in PIT ticks.
    bool xau_set_timer(XAU xau_win, hcpp::u32 timer_id,
                       hcpp::u64 ticks) noexcept;
    bool xau_kill_timer(XAU xau_win, hcpp::u32 timer_id) noexcept;

    // Default window proc: paints the client background on PAINT,
    // destroys the window on CLOSE, swallows everything else.
    hcpp::i32 xau_defproc(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                        hcpp::u64 lparam) noexcept;

}
