#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

void* copy(void* destination, const void* source, size_t bytes) noexcept;
void* move(void* destination, const void* source, size_t bytes) noexcept;
void* set(void* destination, u8 value, size_t bytes) noexcept;
int compare(const void* left, const void* right, size_t bytes) noexcept;

} // namespace hcpp
