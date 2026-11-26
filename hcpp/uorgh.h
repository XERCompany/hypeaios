#pragma once
#include "hcpp/types.hpp"
namespace hcpp::uorgh {
using ObjectHandle = u32;
constexpr ObjectHandle invalid_handle = 0;
void initialize() noexcept;
ObjectHandle allocate(u32 type, void* object) noexcept;
void* resolve(ObjectHandle handle, u32 type) noexcept;
bool release(ObjectHandle handle) noexcept;
size_t active_count() noexcept;
} // namespace hcpp::uorgh
