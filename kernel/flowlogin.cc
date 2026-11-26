#include "kernel/flowlogin.h"

#include "kernel/userservice.h"
namespace hype::users {
    namespace {
        LoginState state = LoginState::LoggedOut;
        hcpp::u32 user;
    }  // namespace
    void login_reset() noexcept {
        state = LoginState::LoggedOut;
        user = 0;
    }
    bool login_begin(hcpp::u32 id) noexcept {
        const User *entry = users_find(id);
        if (entry == nullptr || !entry->active) return false;
        user = id;
        state = LoginState::Authenticating;
        return true;
    }
    void login_authenticated() noexcept {
        if (state == LoginState::Authenticating) state = LoginState::LoggedIn;
    }
    void login_denied() noexcept {
        if (state == LoginState::Authenticating) state = LoginState::Denied;
    }
    LoginState login_state() noexcept { return state; }
    hcpp::u32 login_user() noexcept { return user; }
}  // namespace hype::users

namespace hype::users {
    void login_logout() noexcept {
        state = LoginState::LoggedOut;
        user = 0;
    }
    bool login_is_logged_in() noexcept { return state == LoginState::LoggedIn; }
}  // namespace hype::users
