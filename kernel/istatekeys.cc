#include "kernel/istatekeys.h"
#include "hcpp/bit.h"
namespace hype::input {
hcpp::u32 set_key(hcpp::u32 state, hcpp::u8 key) noexcept { return hcpp::bits::set32(state,key); }
hcpp::u32 clear_key(hcpp::u32 state, hcpp::u8 key) noexcept { return hcpp::bits::clear32(state,key); }
bool key_active(hcpp::u32 state, hcpp::u8 key) noexcept { return hcpp::bits::test32(state,key); }
} // namespace hype::input
