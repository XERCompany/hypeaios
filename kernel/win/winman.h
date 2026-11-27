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
        hype::ui::Rect frame{};
        hype::ui::Rect content_rect{};
        WinState state = WinState::Normal;
        bool maximized = false;
        SnapZone snap = SnapZone::None;
        hype::ui::Rect restore{};
        hcpp::u32 z = 0;
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

}
