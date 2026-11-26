#include "kernel/semaphore.h"
namespace hype::kernel {
    void semaphore_initialize(Semaphore& semaphore, hcpp::u32 initial, hcpp::u32 maximum) noexcept {
        spinlock_initialize(semaphore.gate);
        semaphore.count = initial > maximum ? maximum : initial;
        semaphore.maximum = maximum;
    }
    bool semaphore_try_acquire(Semaphore& semaphore) noexcept {
        bool ok = false;
        spinlock_acquire(semaphore.gate);
        if (semaphore.count != 0) {
            semaphore.count = static_cast<hcpp::u32>(semaphore.count - 1);
            ok = true;
        }
        spinlock_release(semaphore.gate);
        return ok;
    }
    void semaphore_acquire(Semaphore& semaphore) noexcept {
        while (!semaphore_try_acquire(semaphore)) asm volatile("pause");
    }
    bool semaphore_release(Semaphore& semaphore) noexcept {
        bool ok = false;
        spinlock_acquire(semaphore.gate);
        if (semaphore.count < semaphore.maximum) {
            semaphore.count = static_cast<hcpp::u32>(semaphore.count + 1);
            ok = true;
        }
        spinlock_release(semaphore.gate);
        return ok;
    }
    hcpp::u32 semaphore_count(const Semaphore& semaphore) noexcept { return semaphore.count; }
}  // namespace hype::kernel
