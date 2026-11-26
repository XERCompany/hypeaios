#include "kernel/mutex.h"

#include "hcpp/atomic.h"
namespace hype::kernel {
    void mutex_initialize(Mutex& mutex) noexcept {
        spinlock_initialize(mutex.gate);
        mutex.owner = 0;
        mutex.recursion = 0;
    }
    bool mutex_owned(const Mutex& mutex, hcpp::u32 owner) noexcept { return mutex.owner == owner && owner != 0; }
    bool mutex_try_acquire(Mutex& mutex, hcpp::u32 owner) noexcept {
        if (owner == 0) return false;
        if (mutex_owned(mutex, owner)) {
            mutex.recursion = static_cast<hcpp::u32>(mutex.recursion + 1);
            return true;
        }
        if (!spinlock_try_acquire(mutex.gate)) return false;
        mutex.owner = owner;
        mutex.recursion = 1;
        return true;
    }
    void mutex_acquire(Mutex& mutex, hcpp::u32 owner) noexcept {
        while (!mutex_try_acquire(mutex, owner)) asm volatile("pause");
    }
    void mutex_release(Mutex& mutex, hcpp::u32 owner) noexcept {
        if (!mutex_owned(mutex, owner)) return;
        mutex.recursion = static_cast<hcpp::u32>(mutex.recursion - 1);
        if (mutex.recursion == 0) {
            mutex.owner = 0;
            spinlock_release(mutex.gate);
        }
    }
}  // namespace hype::kernel
