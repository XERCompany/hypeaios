#include "kernel/apgsecurtiy.h"
namespace hype::security {
bool capabilities_valid(hcpp::u32 capabilities) noexcept {
  constexpr hcpp::u32 all = capability_read | capability_write |
                            capability_execute | capability_devices |
                            capability_users;
  return (capabilities & ~all) == 0;
}
bool capability_allowed(hcpp::u32 granted, hcpp::u32 required) noexcept {
  return capabilities_valid(granted) && capabilities_valid(required) &&
         (granted & required) == required;
}
} // namespace hype::security

namespace hype::security {
hcpp::u32 capability_grant(hcpp::u32 value, hcpp::u32 permission) noexcept {
  return value | permission;
}
hcpp::u32 capability_revoke(hcpp::u32 value, hcpp::u32 permission) noexcept {
  return value & ~permission;
}
bool capability_any(hcpp::u32 granted, hcpp::u32 required) noexcept {
  return (granted & required) != 0;
}
} // namespace hype::security
