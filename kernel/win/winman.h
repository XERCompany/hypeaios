#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/hxrt/appctx.h"
#include "kernel/ui/geometry.h"

namespace hype::win {

    constexpr hcpp::u32 wm_max_windows = 16;
    constexpr hcpp::i32 wm_chrome_height = 28;
    constexpr hcpp::i32 wm_cascade_step = 40;

    enum class WinState : hcpp::u8 { Normal = 0, Focused = 1, Minimized = 2 };

    // Aero-Snap target: halves, quarters and full work area (Top).
    enum class SnapZone : hcpp::u8 {
        None = 0,
        Left = 1,
        Right = 2,
        Top = 3,
        Nw = 4,
        Ne = 5,
        Sw = 6,
        Se = 7
    };

    struct Win {
        hcpp::u32 id = 0;
        char app_name[24]{};
        // Optional caption override (XAU windows carry a synthetic
        // app_name; the user-facing title lives here). Empty = app_name.
        char caption[24]{};
        hype::ui::Rect frame{};
        hype::ui::Rect content_rect{};
        WinState state = WinState::Normal;
        bool maximized = false;
        SnapZone snap = SnapZone::None;
        hype::ui::Rect restore{};
        hcpp::u32 z = 0;
        // Virtual-desktop tag (issue #135): 0-based index of the desktop
        // this window lives on. Only windows tagged with vdesk_current()
        // paint, hit-test, enumerate in the taskbar and enter the Alt-Tab
        // MRU list.
        hcpp::u8 vdesk = 0;
        hype::hxrt::AppCtx ctx{};
    };

    // Generic right-click context menu (desktop shell + icon menus).
    // Items with wm_ctx_separator render as a 1 px divider; items with
    // wm_ctx_disabled render muted and never dispatch.
    constexpr hcpp::u8 wm_ctx_separator = 1;
    constexpr hcpp::u8 wm_ctx_disabled = 2;
    constexpr hcpp::u32 wm_ctx_max_items = 8;

    struct WmCtxItem {
        const char* label;
        hcpp::u32 id;
        hcpp::u8 flags;
    };

    using WmCtxPick = void (*)(hcpp::u32 id, void* user) noexcept;

    hcpp::u32 wm_open(const char* app_name, hype::ui::Rect frame) noexcept;
    bool wm_close(hcpp::u32 id) noexcept;
    bool wm_focus(hcpp::u32 id) noexcept;
    bool wm_set_minimized(hcpp::u32 id, bool minimized) noexcept;
    bool wm_set_maximized(hcpp::u32 id, bool maximized) noexcept;
    bool wm_snap(hcpp::u32 id, SnapZone zone) noexcept;
    void wm_paint(hcpp::graphics::Framebuffer& fb) noexcept;
    void wm_invalidate(hype::ui::Rect region) noexcept;
    void wm_invalidate_app(const char* app_name) noexcept;
    void wm_invalidate_all() noexcept;
    bool wm_dirty() noexcept;
    hype::ui::Rect wm_damage_clip(hype::ui::Rect bounds) noexcept;
    bool wm_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept;
    bool wm_handle_mouse_r(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool wm_handle_move(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool wm_drag_active() noexcept;
    bool wm_handle_key(char ascii) noexcept;
    bool wm_test_click(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool wm_menu_open(hcpp::u32 id) noexcept;
    void wm_menu_close() noexcept;
    bool wm_menu_visible() noexcept;
    bool wm_ctx_open(hcpp::i32 x, hcpp::i32 y, const WmCtxItem* items,
                     hcpp::u32 count, WmCtxPick pick, void* user) noexcept;
    void wm_ctx_close() noexcept;
    bool wm_ctx_visible() noexcept;
    hcpp::u32 wm_count() noexcept;
    const Win* wm_window(hcpp::u32 id) noexcept;
    const Win* wm_window_at(hcpp::u32 index) noexcept;
    const Win* wm_focused() noexcept;
    bool wm_selftest() noexcept;

    // --- XAU windowing-layer hooks (implemented in kernel/win/xau.cc) ---
    // Event ids reported to the registered xau event hook. Kept as plain
    // constants so this header stays free of the xau types.
    constexpr hcpp::u32 wm_xau_ev_close = 1;
    constexpr hcpp::u32 wm_xau_ev_size = 2;
    constexpr hcpp::u32 wm_xau_ev_move = 3;
    constexpr hcpp::u32 wm_xau_ev_lbuttondown = 4;
    constexpr hcpp::u32 wm_xau_ev_lbuttonup = 5;
    constexpr hcpp::u32 wm_xau_ev_rbuttondown = 6;
    constexpr hcpp::u32 wm_xau_ev_mousemove = 7;
    constexpr hcpp::u32 wm_xau_ev_minimize = 8;
    constexpr hcpp::u32 wm_xau_ev_maximize = 9;
    constexpr hcpp::u32 wm_xau_ev_focus = 10;

    // Paint hook: return true when the window's content was rendered by
    // XAU (skips the hxrt::hxrt_exec fallback). Event hook: return true
    // when the event was consumed (pointer coords are screen space).
    using WmXauPaintHook = bool (*)(hcpp::u32 win_id,
                                    hype::hxrt::AppCtx& ctx) noexcept;
    using WmXauEventHook = bool (*)(hcpp::u32 win_id, hcpp::u32 ev,
                                    hcpp::i32 a, hcpp::i32 b) noexcept;
    void wm_set_xau_hooks(WmXauPaintHook paint,
                          WmXauEventHook event) noexcept;
    // Setters the xau layer needs on top of the read-only wm_window().
    bool wm_set_frame(hcpp::u32 id, hype::ui::Rect frame) noexcept;
    void wm_set_caption(hcpp::u32 id, const char* caption) noexcept;

    // --- Virtual desktops (issue #135) ------------------------------------
    // Multiple workspaces with per-desktop window sets: each Win carries a
    // `vdesk` tag and everything user-visible (paint, hit tests, taskbar
    // buttons, Alt-Tab) filters to the current desktop. Each desktop
    // remembers its focused window across switches. Serial markers:
    // "vdesk switch=<i>/<n>", "vdesk new=N", "vdesk del=N", "vdesk move=<i>"
    // (desktop numbers are 1-based in markers/toasts).
    constexpr hcpp::u32 vdesk_min = 2;
    constexpr hcpp::u32 vdesk_max = 9;
    constexpr hcpp::u32 vdesk_default = 4;
    hcpp::u32 vdesk_count() noexcept;
    hcpp::u32 vdesk_current() noexcept;
    // Switch to desktop `index` (0-based); restores that desktop's
    // remembered focus and repaints. No-op when already there.
    bool vdesk_switch(hcpp::u32 index) noexcept;
    // Relative switch with wraparound (Win+Ctrl+Left/Right).
    bool vdesk_step(hcpp::i32 dir) noexcept;
    // Append a desktop (bounded by vdesk_max) and switch to it.
    bool vdesk_new() noexcept;
    // Delete the current desktop (bounded by vdesk_min); its windows move
    // to the previous desktop — or the next one when deleting desktop 0.
    bool vdesk_delete() noexcept;
    // Move the focused window one desktop over (Win+Shift+Left/Right);
    // focus stays on the current desktop's next topmost window.
    bool vdesk_move_focused(hcpp::i32 dir) noexcept;
    // Settings apply path (ui.vdesk_count): resize the desktop count,
    // migrating windows on dropped desktops to the last surviving one.
    void vdesk_set_count(hcpp::u32 count) noexcept;

}
