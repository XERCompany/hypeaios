#include "kernel/apgsecurtiy.h"

namespace hype::security {
    constexpr hcpp::u32 all_capabilities = capability_read | capability_write | capability_execute |
                                           capability_devices | capability_users | capability_chown |
                                           capability_dac_override | capability_sys_admin;
    bool capabilities_valid(hcpp::u32 capabilities) noexcept { return (capabilities & ~all_capabilities) == 0; }
    bool capability_allowed(hcpp::u32 granted, hcpp::u32 required) noexcept {
        return capabilities_valid(granted) && capabilities_valid(required) && (granted & required) == required;
    }
    hcpp::u32 capability_grant(hcpp::u32 value, hcpp::u32 permission) noexcept { return value | permission; }
    hcpp::u32 capability_revoke(hcpp::u32 value, hcpp::u32 permission) noexcept { return value & ~permission; }
    bool capability_any(hcpp::u32 granted, hcpp::u32 required) noexcept { return (granted & required) != 0; }
    bool credentials_valid(const Credentials &credentials) noexcept {
        return capabilities_valid(credentials.permitted) && capabilities_valid(credentials.effective) &&
               (credentials.effective & ~credentials.permitted) == 0;
    }
    bool credentials_capable(const Credentials &credentials, hcpp::u32 capability) noexcept {
        return credentials_valid(credentials) && (credentials.effective & capability) == capability;
    }
    Credentials credentials_for_user(hcpp::u32 uid, hcpp::u32 gid, hcpp::u32 capabilities) noexcept {
        return {uid, gid, uid, gid, capabilities, capabilities};
    }
}  // namespace hype::security
