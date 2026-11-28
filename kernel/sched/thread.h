#pragma once

#include "hcpp/types.hpp"

// Kernel-thread model for the HypeAI scheduler (issue #137).
//
// Threads are kernel-only: every thread shares the kernel address
// space and runs with its own PMM-allocated kernel stack. Context is
// a saved RSP produced by hype_context_switch (callee-saved registers
// plus the return address) — a thread preempted inside the timer IRQ
// additionally has its full isr_common frame (all GP regs + iret
// frame) parked below that saved RSP on its own stack, so resumption
// is transparent.
//
// Thread-safety note: IRQ paths and legacy global state (serial, VFS,
// UI) are NOT thread-safe. Scheduler threads must stay CPU-only or
// take explicit locks; the boot thread is pinned to the BSP so VFS/UI
// remain effectively BSP-serialized for now.

namespace hype::sched {

    constexpr hcpp::size_t max_threads = 32;
    constexpr hcpp::size_t thread_stack_pages = 4;  // 16 KiB per thread
    constexpr hcpp::u8 affinity_any = 0xFF;
    constexpr hcpp::u8 cpu_none = 0xFF;

    enum class ThreadState : hcpp::u8 {
        Free = 0,   // table slot available
        Spawning,   // slot reserved, stack/context being built
        Ready,      // on the global runqueue
        Running,    // executing on running_cpu
        Sleeping,   // parked until wake_at_ns
        Dead,       // exited; stack reclaimable via join
        Idle,       // per-CPU idle context (never on the runqueue)
    };

    using ThreadFn = void (*)(void* arg) noexcept;

    struct Thread {
        hcpp::u64 rsp;                  // hype_context_switch frame
        ThreadFn entry;
        void* arg;
        hcpp::uintptr_t stack_base;     // 0 for the adopted boot/idle contexts
        hcpp::u32 stack_pages;
        hcpp::u32 id;
        volatile hcpp::u32 state;       // ThreadState
        volatile hcpp::u32 running_cpu; // cpu_none when not dispatched
        hcpp::u8 affinity;              // target cpu index or affinity_any
        hcpp::u8 is_idle;
        hcpp::u16 reserved;
        char name[12];
        hcpp::u64 wake_at_ns;           // Sleeping: uptime_ns deadline
        volatile hcpp::u64 ticks;       // timeslices consumed
    };

    // Allocates a 16 KiB stack, seeds a hype_context_switch frame so the
    // first dispatch enters thread_entry() -> fn(arg) -> sched::exit(),
    // and queues the thread. affinity pins it to one CPU index or
    // affinity_any lets any CPU's tick pick it up.
    Thread* create_thread(ThreadFn fn, void* arg, const char* name,
                          hcpp::u8 affinity = affinity_any) noexcept;

    // Voluntary reschedule: requeues the caller at the runqueue tail.
    void yield() noexcept;

    // Parks the caller until uptime_ns() reaches now + ms. Wakeups are
    // performed by whichever CPU's LAPIC timer tick scans the table.
    void sleep_ms(hcpp::u64 ms) noexcept;

    // Marks the caller Dead and switches away forever; the stack is
    // reclaimed by join() (or leaks if never joined).
    [[noreturn]] void exit() noexcept;

    // Waits (via yield) until `t` is Dead, then frees its kernel stack
    // and returns the table slot to Free. Returns false when the
    // bounded wait elapses without the thread dying.
    bool join(Thread* t) noexcept;

    // The calling CPU's current thread, or nullptr before adoption.
    Thread* current() noexcept;

    // Timer-tick entry, invoked on every CPU from the IRQ0 handler
    // (interrupts already off). Safe before scheduler_initialize().
    void tick() noexcept;

    // Adopts the calling CPU's current context as its idle thread.
    // Called by APs from their bring-up park loop — may run before
    // scheduler_initialize(); it only touches per-CPU fields.
    void adopt_cpu() noexcept;

    // BSP bring-up (stage sched.context): wraps the boot context as
    // thread 0 pinned to CPU0, seeds the BSP idle thread, arms ticks.
    void scheduler_initialize() noexcept;

    // Spawns the worker set, joins it and prints the serial markers
    // "sched threads=", "sched cpus=", "sched preempt=", "sched ctx=".
    bool scheduler_selftest() noexcept;

    hcpp::size_t scheduler_thread_count() noexcept;
    hcpp::u64 scheduler_context_switches() noexcept;
    bool scheduler_ready() noexcept;

}  // namespace hype::sched
