#include "kernel/sched/internal.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/smp.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

// Preemptive SMP scheduler (issue #137). Design notes:
//
//  * Single global runqueue under rq_lock — plenty for <= 8 CPUs.
//    Every CPU's LAPIC timer IRQ lands on vector 32 and calls tick();
//    thread-context entry points (yield/sleep/exit/join) cli first, so
//    the critical section is never re-entered on the same CPU.
//  * Context switches all flow through hype_context_switch
//    (kernel/arch/x86/context.asm): callee-saved regs + return address
//    are swapped; a thread preempted inside the timer IRQ additionally
//    parks its full isr_common frame + iret frame below the saved RSP
//    on its own stack. The IRQ0 EOI is sent before dispatch (see
//    interrupts/idt.cc) so the frame can be abandoned mid-handler and
//    completed later, possibly on another CPU.
//  * rq_lock is held across the switch; the thread that lands on the
//    CPU releases it — either by resuming inside its own
//    schedule_locked() epilogue or, for freshly seeded contexts, at
//    the top of thread_entry()/idle_entry().
//  * Each CPU keeps a private idle thread: the APs adopt their
//    bring-up park loop, the BSP gets a PMM stack seeded to enter
//    idle_entry(). Idle threads are never queued.
//  * Sleeping threads sit in the table with a wake_at_ns deadline;
//    every tick scans the (bounded) table and requeues due sleepers.
//  * The boot context becomes CPU0's thread 0, pinned to the BSP so
//    the not-thread-safe VFS/UI/serial paths stay BSP-serialized.

extern "C" void hype_context_switch(hcpp::u64* save_rsp, hcpp::u64 restore_rsp) noexcept;

namespace hype::sched {

    kernel::Spinlock rq_lock;
    kernel::Spinlock alloc_lock;
    Thread threads[max_threads];
    Thread idle_threads[x86::smp_max_cpus];
    CpuSched cpu_sched[x86::smp_max_cpus];
    volatile hcpp::u64 total_switches;
    volatile hcpp::u32 sched_armed;

    namespace {
        Thread* rq[max_threads];  // ready ring
        hcpp::size_t rq_head;
        hcpp::size_t rq_count;
        hcpp::u32 next_thread_id = 1;
        bool initialized;

        void copy_name(char* dst, const char* src) noexcept {
            hcpp::size_t i = 0;
            if (src != nullptr)
                while (src[i] != '\0' && i < 11) {
                    dst[i] = src[i];
                    ++i;
                }
            dst[i] = '\0';
        }

        // Stack seed matching hype_context_switch's pop order:
        // r15 r14 r13 r12 rbx rbp (zeros) then the ret target (entry).
        hcpp::u64 seed_context(hcpp::uintptr_t base, hcpp::u32 pages,
                               void (*entry)() noexcept) noexcept {
            auto* top = reinterpret_cast<hcpp::u64*>(
                base + static_cast<hcpp::u64>(pages) * memory::page_size);
            --top;
            *top = reinterpret_cast<hcpp::u64>(entry);
            for (int i = 0; i < 6; ++i) {
                --top;
                *top = 0;
            }
            return reinterpret_cast<hcpp::u64>(top);
        }

        // BSP idle thread body — reached via a seeded switch frame, so
        // it must complete the lock handoff before halting.
        void idle_entry() noexcept {
            kernel::spinlock_release(rq_lock);
            hype_cpu_enable_interrupts();
            for (;;) hype_cpu_halt();
        }

        Thread* dequeue_locked(hcpp::size_t cpu) noexcept {
            for (hcpp::size_t i = 0; i < rq_count; ++i) {
                const hcpp::size_t idx = (rq_head + i) % max_threads;
                Thread* t = rq[idx];
                if (t->affinity != affinity_any && t->affinity != cpu) continue;
                for (hcpp::size_t j = i; j + 1 < rq_count; ++j)
                    rq[(rq_head + j) % max_threads] = rq[(rq_head + j + 1) % max_threads];
                --rq_count;
                return t;
            }
            return nullptr;
        }

        void wake_sleepers_locked() noexcept {
            const hcpp::u64 now = time::uptime_ns();
            for (auto& t : threads) {
                if (t.state == static_cast<hcpp::u32>(ThreadState::Sleeping) &&
                    now >= t.wake_at_ns) {
                    t.state = static_cast<hcpp::u32>(ThreadState::Ready);
                    (void)enqueue_locked(&t);
                }
            }
        }
    }  // namespace

    // First entry point of every create_thread() thread — same
    // lock-handoff duty as idle_entry(), then runs the payload.
    void thread_entry() noexcept {
        kernel::spinlock_release(rq_lock);
        hype_cpu_enable_interrupts();
        Thread* self = current();
        if (self != nullptr && self->entry != nullptr) self->entry(self->arg);
        exit();
    }

    hcpp::size_t this_cpu() noexcept { return x86::smp_cpu_id(); }

    hcpp::u64 irq_save() noexcept {
        hcpp::u64 flags;
        asm volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flags)::"cc", "memory");
        return flags;
    }

    void irq_restore(hcpp::u64 flags) noexcept {
        asm volatile("pushq %0\n\tpopfq" ::"r"(flags) : "cc", "memory");
    }

    bool enqueue_locked(Thread* t) noexcept {
        if (rq_count >= max_threads) return false;
        rq[(rq_head + rq_count) % max_threads] = t;
        ++rq_count;
        return true;
    }

    void schedule_locked(hcpp::size_t cpu) noexcept {
        CpuSched& cs = cpu_sched[cpu];
        Thread* cur = cs.current;
        if (cur == nullptr) return;

        wake_sleepers_locked();

        Thread* next = dequeue_locked(cpu);
        if (next == nullptr) {
            if (cur->state == static_cast<hcpp::u32>(ThreadState::Running)) return;
            next = cs.idle;  // cur blocked or died: fall back to idle
            if (next == nullptr || next == cur) return;
        }
        if (next == cur) return;

        if (cur->is_idle != 0) {
            cur->state = static_cast<hcpp::u32>(ThreadState::Idle);
        } else if (cur->state == static_cast<hcpp::u32>(ThreadState::Running)) {
            cur->state = static_cast<hcpp::u32>(ThreadState::Ready);
            (void)enqueue_locked(cur);
        }
        cur->running_cpu = cpu_none;
        next->running_cpu = static_cast<hcpp::u32>(cpu);
        next->state = static_cast<hcpp::u32>(ThreadState::Running);
        cs.current = next;
        ++cs.switches;
        __atomic_fetch_add(&total_switches, 1ull, __ATOMIC_RELAXED);
        hype_context_switch(&cur->rsp, next->rsp);
    }

    void tick() noexcept {
        if (__atomic_load_n(&sched_armed, __ATOMIC_ACQUIRE) == 0) return;
        const hcpp::size_t cpu = this_cpu();
        if (cpu >= x86::smp_max_cpus || cpu_sched[cpu].active == 0) return;
        kernel::spinlock_acquire(rq_lock);
        Thread* cur = cpu_sched[cpu].current;
        if (cur != nullptr) cur->ticks = cur->ticks + 1;
        schedule_locked(cpu);
        kernel::spinlock_release(rq_lock);
    }

    void adopt_cpu() noexcept {
        const hcpp::size_t cpu = this_cpu();
        if (cpu >= x86::smp_max_cpus) return;
        Thread& idle = idle_threads[cpu];
        if (idle.state != static_cast<hcpp::u32>(ThreadState::Free)) return;
        idle = {};
        idle.id = 0x100u + static_cast<hcpp::u32>(cpu);
        idle.is_idle = 1;
        idle.affinity = static_cast<hcpp::u8>(cpu);
        idle.state = static_cast<hcpp::u32>(ThreadState::Running);
        idle.running_cpu = static_cast<hcpp::u32>(cpu);
        idle.stack_base = 0;  // runs on the AP bring-up stack
        copy_name(idle.name, "idle");
        idle.name[4] = static_cast<char>('0' + (cpu % 10));
        cpu_sched[cpu].idle = &idle;
        cpu_sched[cpu].current = &idle;
        cpu_sched[cpu].active = 1;
    }

    void scheduler_initialize() noexcept {
        if (initialized) return;
        kernel::spinlock_initialize(rq_lock);
        kernel::spinlock_initialize(alloc_lock);
        for (auto& t : threads) t = {};
        rq_head = 0;
        rq_count = 0;
        total_switches = 0;

        // CPU0: adopt the boot context as thread 0, pinned to the BSP.
        Thread& boot = threads[0];
        boot = {};
        boot.id = next_thread_id++;
        boot.state = static_cast<hcpp::u32>(ThreadState::Running);
        boot.running_cpu = 0;
        boot.affinity = 0;
        copy_name(boot.name, "boot");
        cpu_sched[0].current = &boot;
        cpu_sched[0].active = 1;

        // BSP idle thread on a real seeded stack.
        Thread& idle0 = idle_threads[0];
        idle0 = {};
        idle0.id = 0x100;
        idle0.is_idle = 1;
        idle0.affinity = 0;
        idle0.running_cpu = cpu_none;
        idle0.state = static_cast<hcpp::u32>(ThreadState::Idle);
        copy_name(idle0.name, "idle0");
        const hcpp::uintptr_t stack = memory::pmm_allocate_pages(thread_stack_pages);
        if (stack != 0) {
            idle0.stack_base = stack;
            idle0.stack_pages = thread_stack_pages;
            idle0.rsp = seed_context(stack, thread_stack_pages, idle_entry);
            cpu_sched[0].idle = &idle0;
        }

        initialized = true;
        __atomic_store_n(&sched_armed, 1u, __ATOMIC_RELEASE);
    }

    bool scheduler_ready() noexcept {
        return __atomic_load_n(&sched_armed, __ATOMIC_ACQUIRE) != 0;
    }

    Thread* current() noexcept {
        const hcpp::size_t cpu = this_cpu();
        return cpu < x86::smp_max_cpus ? cpu_sched[cpu].current : nullptr;
    }

    hcpp::size_t scheduler_thread_count() noexcept {
        hcpp::size_t n = 0;
        for (const auto& t : threads)
            if (t.state != static_cast<hcpp::u32>(ThreadState::Free)) ++n;
        return n;
    }

    hcpp::u64 scheduler_context_switches() noexcept {
        return __atomic_load_n(&total_switches, __ATOMIC_RELAXED);
    }

    // ---- preemptive selftest -------------------------------------
    //
    // Workers burn a fixed counter budget without ever yielding: on a
    // shared CPU the only way two of them both finish is the LAPIC
    // timer switching between them. workers[0] and workers[1] are
    // pinned to CPU0 (alongside the boot thread) to prove preemption;
    // workers 2..3 are pinned to APs to prove SMP spread.

    namespace {
        struct Worker {
            volatile hcpp::u64 counter;
            hcpp::u64 target;
            volatile hcpp::u64 cpu_mask;
        };

        void worker_fn(void* arg) noexcept {
            auto* w = static_cast<Worker*>(arg);
            while (w->counter < w->target) {
                w->counter = w->counter + 1;
                if ((w->counter & 0xFFFFFull) == 0)
                    w->cpu_mask |= 1ull << (this_cpu() & 63);
            }
        }

        volatile hcpp::u32 sleeper_done;
        void sleeper_fn(void*) noexcept {
            sleep_ms(30);
            sleeper_done = 1;
        }

        void write_mask(hcpp::u64 mask) noexcept { serial::write_hex64(mask); }
    }  // namespace

    bool scheduler_selftest() noexcept {
        if (!initialized) scheduler_initialize();

        const hcpp::u64 switches_before = scheduler_context_switches();
        constexpr int worker_count = 4;
        constexpr hcpp::u64 work_target = 40000000ull;
        const char* names[worker_count] = {"w0", "w1", "w2", "w3"};
        Worker workers[worker_count]{};
        Thread* handles[worker_count]{};
        int spawned = 0;

        for (int i = 0; i < worker_count; ++i) {
            workers[i].target = work_target;
            hcpp::u8 affinity = 0;  // workers 0,1 share CPU0 -> preempt proof
            if (i >= 2) {
                const hcpp::size_t want = static_cast<hcpp::size_t>(i - 1);
                if (want < x86::smp_max_cpus && cpu_sched[want].active != 0)
                    affinity = static_cast<hcpp::u8>(want);
            }
            handles[i] = create_thread(worker_fn, &workers[i], names[i], affinity);
            if (handles[i] != nullptr) ++spawned;
        }

        Thread* sleeper = create_thread(sleeper_fn, nullptr, "slp", affinity_any);
        const hcpp::size_t live_threads = scheduler_thread_count();

        bool joined_all = true;
        for (int i = 0; i < worker_count; ++i)
            if (handles[i] != nullptr && !join(handles[i])) joined_all = false;
        const bool sleep_ok = sleeper != nullptr && join(sleeper) && sleeper_done == 1;

        bool all_done = spawned == worker_count && joined_all;
        hcpp::u64 spread = 0;
        for (const auto& w : workers) {
            if (w.counter < w.target) all_done = false;
            spread |= w.cpu_mask;
        }
        const hcpp::u64 switches = scheduler_context_switches() - switches_before;
        // The two CPU0-pinned workers never yield; both completing
        // requires the timer to have switched between them at least
        // once apiece, and real round-robin produces far more.
        const bool preempt_ok = all_done && switches >= worker_count;

        serial::write("sched threads=");
        serial::write_dec32(static_cast<hcpp::u32>(live_threads));
        serial::write(" cpus=");
        serial::write_dec32(static_cast<hcpp::u32>(x86::smp_cpu_count()));
        serial::write("\r\n");
        serial::write("sched sleep=");
        serial::write_line(sleep_ok ? "ok" : "fail");
        serial::write("sched preempt=");
        serial::write_line(preempt_ok ? "ok" : "fail");
        serial::write("sched ctx=");
        serial::write_dec32(static_cast<hcpp::u32>(switches));
        serial::write(" spread=");
        write_mask(spread);
        serial::write("\r\n");
        return sleep_ok && preempt_ok;
    }

}  // namespace hype::sched
