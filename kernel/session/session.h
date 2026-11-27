#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::session {

    enum class SessionState : hcpp::u8 {
        Booting,
        Greeter,
        Registering,
        Authenticating,
        LoggedIn,
        Locked,
    };

    SessionState session_state() noexcept;
    const char* session_state_name(SessionState state) noexcept;
    hcpp::u32 session_user() noexcept;
    const char* session_user_name() noexcept;
    bool session_logged_in() noexcept;
    bool session_locked() noexcept;

    // Lock/unlock the desktop session. Locking keeps every window alive
    // under a full-screen veil; Enter or a click returns to LoggedIn.
    void session_lock() noexcept;
    void session_unlock() noexcept;

    // Centered power modal (Shutdown / Restart / Cancel). Esc or a
    // click outside dismisses it.
    void session_power_dialog() noexcept;
    bool session_power_dialog_active() noexcept;
    void session_power_dialog_close() noexcept;

    // Per-tick hook while Locked: repaints the veil on entry and once
    // per RTC minute. Returns true when pixels were painted.
    bool session_frame(hcpp::graphics::Framebuffer& fb) noexcept;

    hcpp::u32 session_register(hcpp::StringView name, const void* password,
                               hcpp::size_t password_size, const void* pin,
                               hcpp::size_t pin_size) noexcept;

    hcpp::u32 session_begin(hcpp::graphics::Framebuffer& fb) noexcept;
    hcpp::u32 session_scripted_auth(hcpp::graphics::Framebuffer& fb) noexcept;
    bool session_handle_char(char ascii) noexcept;

}
