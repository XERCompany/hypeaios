#include "kernel/immtdevuser.h"
namespace hype::immt {
    bool valid_device_user(const DeviceUser& user) noexcept { return user.user_id != 0 && user.device_id != 0; }
    bool device_user_has(const DeviceUser& user, hcpp::u32 permission) noexcept {
        return valid_device_user(user) && (user.permissions & permission) == permission;
    }
}  // namespace hype::immt
