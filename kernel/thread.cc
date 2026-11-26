#include "kernel/thread.h"
namespace hype::kernel {
    void thread_initialize(ThreadInfo& thread) noexcept {
        lmh::task_reset(thread.task);
        thread.priority_class = ThreadPriorityClass::Normal;
        thread.cpu_time = 0;
        thread.quantum = thread_quantum(thread.priority_class);
    }
    ThreadPriorityClass thread_priority_class(hcpp::u8 priority) noexcept {
        if (priority < 5) return ThreadPriorityClass::Realtime;
        if (priority < 12) return ThreadPriorityClass::High;
        if (priority < 20) return ThreadPriorityClass::AboveNormal;
        if (priority < 30) return ThreadPriorityClass::Normal;
        if (priority < 35) return ThreadPriorityClass::BelowNormal;
        return ThreadPriorityClass::Idle;
    }
    hcpp::u32 thread_quantum(ThreadPriorityClass c) noexcept {
        switch (c) {
            case ThreadPriorityClass::Realtime:
                return 2;
            case ThreadPriorityClass::High:
                return 4;
            case ThreadPriorityClass::AboveNormal:
                return 6;
            case ThreadPriorityClass::Normal:
                return 8;
            case ThreadPriorityClass::BelowNormal:
                return 12;
            default:
                return 16;
        }
    }
    void thread_apply_priority(ThreadInfo& thread, ThreadPriorityClass c) noexcept {
        thread.priority_class = c;
        thread.quantum = thread_quantum(c);
        thread.task.time_slice = thread.quantum;
    }
}  // namespace hype::kernel
