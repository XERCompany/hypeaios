#pragma once
#include "kernel/spinlock.h"
namespace hype::kernel {
    struct Semaphore {
        Spinlock gate;
        volatile hcpp::u32 count;
        hcpp::u32 maximum;
    };
    void semaphore_initialize(Semaphore& semaphore, hcpp::u32 initial, hcpp::u32 maximum) noexcept;
    bool semaphore_try_acquire(Semaphore& semaphore) noexcept;
    void semaphore_acquire(Semaphore& semaphore) noexcept;
    bool semaphore_release(Semaphore& semaphore) noexcept;
    hcpp::u32 semaphore_count(const Semaphore& semaphore) noexcept;
}  // namespace hype::kernel
