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
    };

    SessionState session_state() noexcept;
    const char* session_state_name(SessionState state) noexcept;
    hcpp::u32 session_user() noexcept;
    bool session_logged_in() noexcept;

    hcpp::u32 session_register(hcpp::StringView name, const void* password,
                               hcpp::size_t password_size, const void* pin,
                               hcpp::size_t pin_size) noexcept;

    hcpp::u32 session_begin(hcpp::graphics::Framebuffer& fb) noexcept;
    hcpp::u32 session_scripted_auth(hcpp::graphics::Framebuffer& fb) noexcept;
    bool session_handle_char(char ascii) noexcept;

}
