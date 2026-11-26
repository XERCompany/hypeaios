#pragma once
#include "hcpp/types.hpp"
namespace hype::input {
enum class KeyState : hcpp::u8 { Released = 0, Pressed = 1, Repeated = 2 };
hcpp::u32 set_key(hcpp::u32 state, hcpp::u8 key) noexcept;
hcpp::u32 clear_key(hcpp::u32 state, hcpp::u8 key) noexcept;
bool key_active(hcpp::u32 state, hcpp::u8 key) noexcept;
} // namespace hype::input

namespace hype::input {
hcpp::u32 clear_all_keys() noexcept;
hcpp::u8 active_key_count(hcpp::u32 state) noexcept;
} // namespace hype::input
