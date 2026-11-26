#include "kernel/spinlock.h"

#include "hcpp/atomic.h"
namespace hype::kernel {
    void spinlock_initialize(Spinlock &lock) noexcept { lock.value = 0; }
    void spinlock_acquire(Spinlock &lock) noexcept {
        while (!spinlock_try_acquire(lock)) asm volatile("pause");
    }
    bool spinlock_try_acquire(Spinlock &lock) noexcept { return hcpp::atomic::exchange32(&lock.value, 1) == 0; }
    void spinlock_release(Spinlock &lock) noexcept { hcpp::atomic::store32(&lock.value, 0); }
    SpinlockGuard::SpinlockGuard(Spinlock &lock) noexcept : lock_(&lock) { spinlock_acquire(*lock_); }
    SpinlockGuard::~SpinlockGuard() noexcept {
        if (lock_ != nullptr) spinlock_release(*lock_);
    }
}  // namespace hype::kernel
