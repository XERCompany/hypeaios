#include "kernel/istatekeys.h"

#include "hcpp/bit.h"
namespace hype::input {
hcpp::u32 set_key(hcpp::u32 state, hcpp::u8 key) noexcept {
  return hcpp::bits::set32(state, key);
}
hcpp::u32 clear_key(hcpp::u32 state, hcpp::u8 key) noexcept {
  return hcpp::bits::clear32(state, key);
}
bool key_active(hcpp::u32 state, hcpp::u8 key) noexcept {
  return hcpp::bits::test32(state, key);
}
} // namespace hype::input

namespace hype::input {
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
