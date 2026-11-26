#pragma once
#include "hcpp/types.hpp"
namespace hype::kernel {
    struct Spinlock {
        volatile hcpp::u32 value;
    };
    void spinlock_initialize(Spinlock &lock) noexcept;
    void spinlock_acquire(Spinlock &lock) noexcept;
    bool spinlock_try_acquire(Spinlock &lock) noexcept;
    void spinlock_release(Spinlock &lock) noexcept;
    class SpinlockGuard {
    public:
        explicit SpinlockGuard(Spinlock &lock) noexcept;
        ~SpinlockGuard() noexcept;

    private:
        Spinlock *lock_;
    };
}  // namespace hype::kernel
