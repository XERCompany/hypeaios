#pragma once
#include "kernel/spinlock.h"
namespace hype::kernel {
    struct Mutex {
        Spinlock gate;
        volatile hcpp::u32 owner;
        volatile hcpp::u32 recursion;
    };
    void mutex_initialize(Mutex& mutex) noexcept;
    void mutex_acquire(Mutex& mutex, hcpp::u32 owner) noexcept;
    bool mutex_try_acquire(Mutex& mutex, hcpp::u32 owner) noexcept;
    void mutex_release(Mutex& mutex, hcpp::u32 owner) noexcept;
    bool mutex_owned(const Mutex& mutex, hcpp::u32 owner) noexcept;
}  // namespace hype::kernel
