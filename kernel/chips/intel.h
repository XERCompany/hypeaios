#pragma once
#include "hcpp/types.hpp"
namespace hype::chips::intel {
    const char* device_name(hcpp::u16 device_id) noexcept;
    bool is_host_bridge(hcpp::u16 device_id) noexcept;
    bool is_isa_bridge(hcpp::u16 device_id) noexcept;
}  // namespace hype::chips::intel
