#pragma once
#include "hcpp/types.hpp"
namespace hype::immt {
    struct DeviceUser {
        hcpp::u32 user_id;
        hcpp::u32 device_id;
        hcpp::u32 permissions;
    };
    bool valid_device_user(const DeviceUser& user) noexcept;
    bool device_user_has(const DeviceUser& user, hcpp::u32 permission) noexcept;
}  // namespace hype::immt
