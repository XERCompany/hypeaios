#include "kernel/istatekeys.h"

namespace hype::input {

void keybit_clear_all(KeyBitset &bits) noexcept {
  for (auto &word : bits.words)
    word = 0;
}
void keybit_set(KeyBitset &bits, hcpp::u16 key) noexcept {
  if (key <= max_key_code)
    bits.words[key / 64] |= hcpp::u64(1) << (key % 64);
}
void keybit_clear(KeyBitset &bits, hcpp::u16 key) noexcept {
  if (key <= max_key_code)
    bits.words[key / 64] &= ~(hcpp::u64(1) << (key % 64));
}
bool keybit_test(const KeyBitset &bits, hcpp::u16 key) noexcept {
  return key <= max_key_code &&
         (bits.words[key / 64] & (hcpp::u64(1) << (key % 64))) != 0;
}
hcpp::u16 keybit_count(const KeyBitset &bits) noexcept {
  hcpp::u16 count = 0;
  for (hcpp::u64 word : bits.words)
    while (word != 0) {
      count = static_cast<hcpp::u16>(count + (word & 1));
      word >>= 1;
    }
  return count;
}

hcpp::u32 set_key(hcpp::u32 state, hcpp::u8 key) noexcept {
  return key < 32 ? state | (hcpp::u32(1) << key) : state;
}
hcpp::u32 clear_key(hcpp::u32 state, hcpp::u8 key) noexcept {
  return key < 32 ? state & ~(hcpp::u32(1) << key) : state;
}
bool key_active(hcpp::u32 state, hcpp::u8 key) noexcept {
  return key < 32 && (state & (hcpp::u32(1) << key)) != 0;
}
hcpp::u32 clear_all_keys() noexcept { return 0; }
hcpp::u8 active_key_count(hcpp::u32 state) noexcept {
  hcpp::u8 count = 0;
  while (state != 0) {
    count = static_cast<hcpp::u8>(count + (state & 1));
    state >>= 1;
  }
  return count;
}

} // namespace hype::input
