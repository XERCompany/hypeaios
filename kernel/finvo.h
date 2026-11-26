#pragma once
#include "hcpp/types.hpp"
namespace hype::invoke { enum class InvocationState : hcpp::u8 { Empty, Pending, Completed, Failed }; struct Invocation { hcpp::u32 operation; hcpp::u32 argument; hcpp::u32 result; InvocationState state; }; void invocation_reset(Invocation& invocation) noexcept; bool invocation_begin(Invocation& invocation, hcpp::u32 operation, hcpp::u32 argument) noexcept; void invocation_complete(Invocation& invocation, hcpp::u32 result) noexcept; void invocation_fail(Invocation& invocation) noexcept; }
