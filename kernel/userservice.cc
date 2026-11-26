#include "kernel/userservice.h"

#include "kernel/usergen.h"
namespace hype::users {
namespace {
constexpr hcpp::size_t capacity = 8;
User users[capacity];
hcpp::size_t count;
} // namespace
void users_initialize() noexcept {
  count = 0;
  for (auto &u : users)
    u = {};
  usergen_initialize(0x48595045u);
}
hcpp::u32 users_create(hcpp::StringView name, hcpp::u32 capabilities) noexcept {
  if (name.empty() || count >= capacity)
    return 0;
  const hcpp::u32 id = usergen_next();
  users[count++] = {id, name, capabilities, true};
  return id;
}
const User *users_find(hcpp::u32 id) noexcept {
  for (hcpp::size_t i = 0; i < count; ++i)
    if (users[i].id == id)
      return &users[i];
  return nullptr;
}
bool users_set_active(hcpp::u32 id, bool active) noexcept {
  for (hcpp::size_t i = 0; i < count; ++i)
    if (users[i].id == id) {
      users[i].active = active;
      return true;
    }
  return false;
}
} // namespace hype::users

namespace hype::users {
hcpp::size_t users_count() noexcept { return count; }
bool users_deactivate(hcpp::u32 id) noexcept {
  return users_set_active(id, false);
}
} // namespace hype::users
