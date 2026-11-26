#pragma once
#include "hcpp/types.hpp"
namespace hype::chips::virtio {
    const char* device_name(hcpp::u16 device_id) noexcept;
    hcpp::u8 modern_device_type(hcpp::u16 device_id) noexcept;
}  // namespace hype::chips::virtio
