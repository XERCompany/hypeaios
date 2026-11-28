#include "kernel/sched/internal.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/memory/pmm.h"
#include "kernel/time/timer.h"

// Thread lifecycle for the HypeAI scheduler (issue #137): creation,
// voluntary yield, timed sleep, exit and join. The actual pick-and-
// switch logic lives in scheduler.cc (schedule_locked); every entry
// point here wraps it in irq_save + rq_lock. PMM stack alloc/free
// runs under alloc_lock in thread context only — never under rq_lock
// and never inside IRQ context — so the timer tick can spin on
// rq_lock without ever needing the PMM.

namespace hype::sched {

    namespace {
        hcpp::u64 seed_thread_stack(hcpp::uintptr_t base, hcpp::u32 pages,
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

        void copy_name(char* dst, const char* src) noexcept {
            hcpp::size_t i = 0;
            if (src != nullptr)
                while (src[i] != '\0' && i < 11) {
                    dst[i] = src[i];
                    ++i;
                }
            dst[i] = '\0';
        }

    }  // namespace

    Thread* create_thread(ThreadFn fn, void* arg, const char* name,
                          hcpp::u8 affinity) noexcept {
        if (!scheduler_ready() || fn == nullptr) return nullptr;

        hcpp::u64 flags = irq_save();
        kernel::spinlock_acquire(rq_lock);
        Thread* t = nullptr;
        for (auto& slot : threads) {
            if (slot.state == static_cast<hcpp::u32>(ThreadState::Free)) {
                slot.state = static_cast<hcpp::u32>(ThreadState::Spawning);
                t = &slot;
                break;
            }
        }
        kernel::spinlock_release(rq_lock);
        irq_restore(flags);
        if (t == nullptr) return nullptr;

        kernel::spinlock_acquire(alloc_lock);
        const hcpp::uintptr_t stack = memory::pmm_allocate_pages(thread_stack_pages);
        kernel::spinlock_release(alloc_lock);
        if (stack == 0) {
            flags = irq_save();
            kernel::spinlock_acquire(rq_lock);
            t->state = static_cast<hcpp::u32>(ThreadState::Free);
            kernel::spinlock_release(rq_lock);
            irq_restore(flags);
            return nullptr;
        }

        // Fill the reserved slot field by field — a blanket reset would
        // momentarily drop state back to Free and let a racing
        // create_thread() on another CPU double-book the slot.
        t->rsp = 0;
        t->entry = fn;
        t->arg = arg;
        t->stack_base = stack;
        t->stack_pages = thread_stack_pages;
        t->affinity = affinity;
        t->is_idle = 0;
        t->reserved = 0;
        t->running_cpu = cpu_none;
        t->wake_at_ns = 0;
        t->ticks = 0;
        copy_name(t->name, name);
        t->rsp = seed_thread_stack(stack, thread_stack_pages, thread_entry);

        flags = irq_save();
        kernel::spinlock_acquire(rq_lock);
        static hcpp::u32 next_id = 1000;
        t->id = next_id++;
        t->state = static_cast<hcpp::u32>(ThreadState::Ready);
        (void)enqueue_locked(t);
        kernel::spinlock_release(rq_lock);
        irq_restore(flags);
        return t;
    }

    void yield() noexcept {
        if (!scheduler_ready()) return;
        const hcpp::u64 flags = irq_save();
        kernel::spinlock_acquire(rq_lock);
        schedule_locked(this_cpu());
        kernel::spinlock_release(rq_lock);
        irq_restore(flags);
    }

    void sleep_ms(hcpp::u64 ms) noexcept {
        Thread* self = current();
        if (!scheduler_ready() || self == nullptr) return;
        const hcpp::u64 flags = irq_save();
        kernel::spinlock_acquire(rq_lock);
        self->wake_at_ns = time::uptime_ns() + ms * 1000000ull;
        self->state = static_cast<hcpp::u32>(ThreadState::Sleeping);
        schedule_locked(this_cpu());
        kernel::spinlock_release(rq_lock);
        irq_restore(flags);
    }

    void exit() noexcept {
        Thread* self = current();
        if (self != nullptr && scheduler_ready()) {
            const hcpp::u64 flags = irq_save();
            kernel::spinlock_acquire(rq_lock);
            self->state = static_cast<hcpp::u32>(ThreadState::Dead);
            schedule_locked(this_cpu());
            // Unreachable once a switch happened — the resumed thread
            // unwinds through its own epilogue instead. If nothing else
            // could run (no idle), the dead thread falls out here and
            // parks as a last-resort halt loop.
            kernel::spinlock_release(rq_lock);
            irq_restore(flags);
        }
        for (;;) hype_cpu_halt();
    }

    bool join(Thread* t) noexcept {
        if (t == nullptr) return false;
        const hcpp::u64 deadline = time::uptime_ns() + 5000ull * 1000000ull;
        while (t->state != static_cast<hcpp::u32>(ThreadState::Dead)) {
            if (t->state == static_cast<hcpp::u32>(ThreadState::Free) ||
                time::uptime_ns() > deadline)
                return false;
            yield();
        }
        if (t->stack_base != 0) {
            kernel::spinlock_acquire(alloc_lock);
            (void)memory::pmm_free_pages(t->stack_base, t->stack_pages);
            kernel::spinlock_release(alloc_lock);
            t->stack_base = 0;
        }
        const hcpp::u64 flags = irq_save();
        kernel::spinlock_acquire(rq_lock);
        t->state = static_cast<hcpp::u32>(ThreadState::Free);
        kernel::spinlock_release(rq_lock);
        irq_restore(flags);
        return true;
    }

}  // namespace hype::sched
