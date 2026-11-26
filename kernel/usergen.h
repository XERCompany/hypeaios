#pragma once
#include "hcpp/types.hpp"
namespace hype::users { void usergen_initialize(hcpp::u32 seed) noexcept; hcpp::u32 usergen_next() noexcept; }
