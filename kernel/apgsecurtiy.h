#pragma once
#include "hcpp/types.hpp"
namespace hype::security {
    enum Capability : hcpp::u32 {
        capability_read = 1u << 0,
        capability_write = 1u << 1,
        capability_execute = 1u << 2,
        capability_devices = 1u << 3,
        capability_users = 1u << 4
    };
    bool capabilities_valid(hcpp::u32 capabilities) noexcept;
    bool capability_allowed(hcpp::u32 granted, hcpp::u32 required) noexcept;
}  // namespace hype::security
