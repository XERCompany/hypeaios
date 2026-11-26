#pragma once
#include "hcpp/types.hpp"
namespace hcpp::funkit {
using Callback = void (*)(void *context, u32 value) noexcept;
struct FunctionSlot {
  Callback callback;
  void *context;
};
FunctionSlot make_slot(Callback callback, void *context) noexcept;
bool valid(const FunctionSlot &slot) noexcept;
void invoke(const FunctionSlot &slot, u32 value) noexcept;
} // namespace hcpp::funkit
