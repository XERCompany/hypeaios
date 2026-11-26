#pragma once

#include "hcpp/types.hpp"

namespace hype::security {

    enum Capability : hcpp::u32 {
        capability_read = 1u << 0,
        capability_write = 1u << 1,
        capability_execute = 1u << 2,
        capability_devices = 1u << 3,
        capability_users = 1u << 4,
        capability_chown = 1u << 5,
        capability_dac_override = 1u << 6,
        capability_sys_admin = 1u << 7,
    };

    struct Credentials {
        hcpp::u32 uid;
        hcpp::u32 gid;
        hcpp::u32 effective_uid;
        hcpp::u32 effective_gid;
        hcpp::u32 permitted;
        hcpp::u32 effective;
    };

    bool capabilities_valid(hcpp::u32 capabilities) noexcept;
    bool capability_allowed(hcpp::u32 granted, hcpp::u32 required) noexcept;
    hcpp::u32 capability_grant(hcpp::u32 value, hcpp::u32 permission) noexcept;
    hcpp::u32 capability_revoke(hcpp::u32 value, hcpp::u32 permission) noexcept;
    bool capability_any(hcpp::u32 granted, hcpp::u32 required) noexcept;
    bool credentials_valid(const Credentials &credentials) noexcept;
    bool credentials_capable(const Credentials &credentials, hcpp::u32 capability) noexcept;
    Credentials credentials_for_user(hcpp::u32 uid, hcpp::u32 gid, hcpp::u32 capabilities) noexcept;

}  // namespace hype::security
