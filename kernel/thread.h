#pragma once
#include "kernel/lmhtask.h"
namespace hype::kernel {

    enum class ThreadPriorityClass : hcpp::u8 { Idle, BelowNormal, Normal, AboveNormal, High, Realtime };
    struct ThreadInfo {
        lmh::Task task;
        ThreadPriorityClass priority_class;
        hcpp::u64 cpu_time;
        hcpp::u32 quantum;
    };
    void thread_initialize(ThreadInfo& thread) noexcept;
    ThreadPriorityClass thread_priority_class(hcpp::u8 priority) noexcept;
    hcpp::u32 thread_quantum(ThreadPriorityClass priority_class) noexcept;
    void thread_apply_priority(ThreadInfo& thread, ThreadPriorityClass priority_class) noexcept;

}  // namespace hype::kernel
