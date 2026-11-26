#pragma once

#include "hcpp/types.hpp"

namespace hype::input {

enum class KeyState : hcpp::u8 { Released = 0, Pressed = 1, Repeated = 2 };
constexpr hcpp::u16 max_key_code = 255;

struct KeyBitset {
  hcpp::u64 words[4];
};

void keybit_clear_all(KeyBitset &bits) noexcept;
void keybit_set(KeyBitset &bits, hcpp::u16 key) noexcept;
void keybit_clear(KeyBitset &bits, hcpp::u16 key) noexcept;
bool keybit_test(const KeyBitset &bits, hcpp::u16 key) noexcept;
hcpp::u16 keybit_count(const KeyBitset &bits) noexcept;

hcpp::u32 set_key(hcpp::u32 state, hcpp::u8 key) noexcept;
hcpp::u32 clear_key(hcpp::u32 state, hcpp::u8 key) noexcept;
bool key_active(hcpp::u32 state, hcpp::u8 key) noexcept;
hcpp::u32 clear_all_keys() noexcept;
hcpp::u8 active_key_count(hcpp::u32 state) noexcept;

} // namespace hype::input
