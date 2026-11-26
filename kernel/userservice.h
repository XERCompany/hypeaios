#pragma once
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
namespace hype::users {
struct User {
  hcpp::u32 id;
  hcpp::StringView name;
  hcpp::u32 capabilities;
  bool active;
};
void users_initialize() noexcept;
hcpp::u32 users_create(hcpp::StringView name, hcpp::u32 capabilities) noexcept;
const User *users_find(hcpp::u32 id) noexcept;
bool users_set_active(hcpp::u32 id, bool active) noexcept;
} // namespace hype::users

namespace hype::users {
hcpp::size_t users_count() noexcept;
bool users_deactivate(hcpp::u32 id) noexcept;
} // namespace hype::users
