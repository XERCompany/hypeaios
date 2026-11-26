#include "hcpp/bit.h"
namespace hcpp::bits {
u32 set32(u32 value, u8 bit) noexcept { return bit < 32 ? value | (u32(1) << bit) : value; }
u32 clear32(u32 value, u8 bit) noexcept { return bit < 32 ? value & ~(u32(1) << bit) : value; }
u32 toggle32(u32 value, u8 bit) noexcept { return bit < 32 ? value ^ (u32(1) << bit) : value; }
bool test32(u32 value, u8 bit) noexcept { return bit < 32 && (value & (u32(1) << bit)) != 0; }
u32 mask32(u8 bit_count) noexcept { return bit_count == 0 ? 0 : (bit_count >= 32 ? ~u32(0) : ((u32(1) << bit_count) - 1)); }
u8 count32(u32 value) noexcept { u8 count = 0; while (value != 0) { count += static_cast<u8>(value & 1); value >>= 1; } return count; }
u64 set64(u64 value, u8 bit) noexcept { return bit < 64 ? value | (u64(1) << bit) : value; }
bool test64(u64 value, u8 bit) noexcept { return bit < 64 && (value & (u64(1) << bit)) != 0; }
} // namespace hcpp::bits
