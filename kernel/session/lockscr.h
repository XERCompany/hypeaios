#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"

namespace hype::session {

    // Hooks the lock/power key chords (Win+L, Ctrl+Alt+L,
    // Ctrl+Alt+Del/End) on the input bus. Idempotent; safe to call from
    // session_begin before login.
    void lockscr_init() noexcept;

    // Arms a full lock-screen repaint for the next lockscr_frame call.
    void lockscr_enter() noexcept;

    // Repaints the lock veil when needed (first frame after lock, RTC
    // minute rollover). Returns true when pixels were painted so the
    // caller can redraw the cursor sprite on top.
    bool lockscr_frame(hcpp::graphics::Framebuffer& fb) noexcept;

}
