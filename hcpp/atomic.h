#pragma once
#include "hcpp/types.hpp"
namespace hcpp::atomic {
    u32 load32(const volatile u32 *value) noexcept;
    void store32(volatile u32 *target, u32 value) noexcept;
    u32 add32(volatile u32 *target, u32 value) noexcept;
    u32 exchange32(volatile u32 *target, u32 value) noexcept;
    bool compare_exchange32(volatile u32 *target, u32 expected, u32 desired) noexcept;
    u64 load64(const volatile u64 *value) noexcept;
    void store64(volatile u64 *target, u64 value) noexcept;
    u64 add64(volatile u64 *target, u64 value) noexcept;
}  // namespace hcpp::atomic
