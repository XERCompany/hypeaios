#pragma once

#include "kernel/sched/thread.h"
#include "kernel/spinlock.h"

// Shared internals between scheduler.cc (runqueue/tick/CPU adoption)
// and thread.cc (thread lifecycle). Not part of the public API.

namespace hype::sched {

    // One global runqueue lock guards threads[], the ready ring and
    // cpu_sched[]. It is ALWAYS taken with interrupts disabled: tick()
    // runs inside IRQ context, and the thread-context paths cli first.
    // The lock is held across hype_context_switch and released by the
    // thread that lands on the CPU afterwards — either the resumed
    // thread's own epilogue, or thread_entry()/idle_entry() for fresh
    // contexts, which do the equivalent release before sti.
    extern kernel::Spinlock rq_lock;

    // Thread-context lock wrapping PMM stack alloc/free. Never taken
    // inside IRQ context and never nested under rq_lock.
    extern kernel::Spinlock alloc_lock;

    struct CpuSched {
        Thread* current;          // thread owning this CPU right now
        Thread* idle;             // per-CPU fallback, never queued
        volatile hcpp::u32 active; // CPU adopted into the scheduler
        hcpp::u64 switches;        // dispatches performed on this CPU
    };

    extern Thread threads[max_threads];
    extern Thread idle_threads[];
    extern CpuSched cpu_sched[];
    extern volatile hcpp::u64 total_switches;
    extern volatile hcpp::u32 sched_armed;

    hcpp::size_t this_cpu() noexcept;

    // cli + return saved RFLAGS. Every schedule entry point pairs an
    // irq_save() before rq_lock with an irq_restore() after release.
    hcpp::u64 irq_save() noexcept;
    void irq_restore(hcpp::u64 flags) noexcept;

    // Core pick-and-switch. Caller holds rq_lock with IF=0. Requeues
    // the current thread when it stays runnable; falls back to the
    // per-CPU idle when nothing is queued. Switches via
    // hype_context_switch when next != cur.
    void schedule_locked(hcpp::size_t cpu) noexcept;

    // Push a Ready thread onto the global ready ring. rq_lock held.
    bool enqueue_locked(Thread* t) noexcept;

    // Seeded-context entry for freshly created threads: completes the
    // rq_lock handoff, sti's, runs the payload, exits. Defined in
    // scheduler.cc, referenced by seed_thread_stack() in thread.cc.
    void thread_entry() noexcept;

}  // namespace hype::sched
