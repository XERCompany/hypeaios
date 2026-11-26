#pragma once
#include "hcpp/types.hpp"
namespace hype::users {
enum class LoginState : hcpp::u8 {
  LoggedOut,
  IdentityRequired,
  Authenticating,
  LoggedIn,
  Denied
};
void login_reset() noexcept;
bool login_begin(hcpp::u32 user_id) noexcept;
void login_authenticated() noexcept;
void login_denied() noexcept;
LoginState login_state() noexcept;
hcpp::u32 login_user() noexcept;
} // namespace hype::users

namespace hype::users {
void login_logout() noexcept;
bool login_is_logged_in() noexcept;
} // namespace hype::users
