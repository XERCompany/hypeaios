#include "hcpp/atomic.h"
namespace hcpp::atomic {
    u32 load32(const volatile u32 *value) noexcept {
        u32 result;
        asm volatile("movl %1,%0" : "=r"(result) : "m"(*value) : "memory");
        return result;
    }
    void store32(volatile u32 *target, u32 value) noexcept {
        asm volatile("movl %1,%0" : "=m"(*target) : "r"(value) : "memory");
    }
    u32 add32(volatile u32 *target, u32 value) noexcept {
        u32 old = value;
        asm volatile("lock xaddl %0,%1" : "+r"(old), "+m"(*target)::"memory");
        return old;
    }
    u32 exchange32(volatile u32 *target, u32 value) noexcept {
        asm volatile("xchgl %0,%1" : "+r"(value), "+m"(*target)::"memory");
        return value;
    }
    bool compare_exchange32(volatile u32 *target, u32 expected, u32 desired) noexcept {
        u32 before = expected;
        asm volatile("lock cmpxchgl %3,%1" : "+a"(before), "+m"(*target) : "r"(desired) : "memory");
        return before == expected;
    }
    u64 load64(const volatile u64 *value) noexcept {
        u64 result;
        asm volatile("movq %1,%0" : "=r"(result) : "m"(*value) : "memory");
        return result;
    }
    void store64(volatile u64 *target, u64 value) noexcept {
        asm volatile("movq %1,%0" : "=m"(*target) : "r"(value) : "memory");
    }
    u64 add64(volatile u64 *target, u64 value) noexcept {
        u64 old = value;
        asm volatile("lock xaddq %0,%1" : "+r"(old), "+m"(*target)::"memory");
        return old;
    }
}  // namespace hcpp::atomic
