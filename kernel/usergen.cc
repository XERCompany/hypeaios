#include "kernel/usergen.h"
namespace hype::users {
namespace {
hcpp::u32 state = 1;
}
void usergen_initialize(hcpp::u32 seed) noexcept {
  state = seed == 0 ? 1 : seed;
}
hcpp::u32 usergen_next() noexcept {
  state = state * 1664525u + 1013904223u;
  return state;
}
} // namespace hype::users

namespace hype::users {
hcpp::u32 usergen_peek() noexcept { return state * 1664525u + 1013904223u; }
} // namespace hype::users
