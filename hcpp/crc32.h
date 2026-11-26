#pragma once
#include "hcpp/types.hpp"
namespace hcpp {
    u32 crc32_update(u32 crc, const void* data, size_t size) noexcept;
    u32 crc32(const void* data, size_t size) noexcept;
}  // namespace hcpp
