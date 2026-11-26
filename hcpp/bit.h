#pragma once
#include "hcpp/types.hpp"
namespace hcpp::bits {
    u32 set32(u32 value, u8 bit) noexcept;
    u32 clear32(u32 value, u8 bit) noexcept;
    u32 toggle32(u32 value, u8 bit) noexcept;
    bool test32(u32 value, u8 bit) noexcept;
    u32 mask32(u8 bit_count) noexcept;
    u8 count32(u32 value) noexcept;
    u64 set64(u64 value, u8 bit) noexcept;
    bool test64(u64 value, u8 bit) noexcept;
}  // namespace hcpp::bits
