#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/hxrt/appctx.h"
#include "kernel/ui/geometry.h"

namespace hype::win {

    constexpr hcpp::u32 wm_max_windows = 16;
    constexpr hcpp::i32 wm_chrome_height = 28;
    constexpr hcpp::i32 wm_cascade_step = 40;

    enum class WinState : hcpp::u8 { Normal = 0, Focused = 1 };

    struct Win {
        hcpp::u32 id = 0;
        char app_name[24]{};
        hype::ui::Rect frame{};
        hype::ui::Rect content_rect{};
        WinState state = WinState::Normal;
        hcpp::u32 z = 0;
        hype::hxrt::AppCtx ctx{};
    };

    hcpp::u32 wm_open(const char* app_name, hype::ui::Rect frame) noexcept;
    bool wm_close(hcpp::u32 id) noexcept;
    bool wm_focus(hcpp::u32 id) noexcept;
    void wm_paint(hcpp::graphics::Framebuffer& fb) noexcept;
    void wm_invalidate(hype::ui::Rect region) noexcept;
    void wm_invalidate_all() noexcept;
    bool wm_dirty() noexcept;
    hype::ui::Rect wm_damage_clip(hype::ui::Rect bounds) noexcept;
    bool wm_handle_mouse(hcpp::i32 x, hcpp::i32 y, bool pressed) noexcept;
    bool wm_handle_key(char ascii) noexcept;
    bool wm_test_click(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool wm_menu_open(hcpp::u32 id) noexcept;
    void wm_menu_close() noexcept;
    bool wm_menu_visible() noexcept;
    hcpp::u32 wm_count() noexcept;
    const Win* wm_window(hcpp::u32 id) noexcept;
    const Win* wm_focused() noexcept;
    bool wm_selftest() noexcept;

}
