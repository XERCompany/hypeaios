#include "kernel/sched/scheduler.h"
namespace hype::sched {
    namespace {
        constexpr int thread_count = 2;
        constexpr hcpp::size_t stack_size = 4096;
        struct Worker {
            hcpp::u64 rsp;
            hcpp::u32 iterations;
            bool done;
        };
        alignas(16) hcpp::u8 stacks[thread_count][stack_size];
        Worker workers[thread_count];
        hcpp::u64 main_rsp;
        int current;
        hcpp::u32 switches;
        bool initialized;
        extern "C" void hype_context_switch(hcpp::u64*, hcpp::u64) noexcept;
        void worker_entry() noexcept {
            Worker& me = workers[current];
            while (me.iterations < 3) {
                ++me.iterations;
                ++switches;
                hype_context_switch(&me.rsp, main_rsp);
            }
            me.done = true;
            hype_context_switch(&me.rsp, main_rsp);
        }
        hcpp::u64 prepare(void* stack, void (*entry)() noexcept) {
            auto* t = reinterpret_cast<hcpp::u64*>(static_cast<hcpp::u8*>(stack) + stack_size);
            --t;
            *t = reinterpret_cast<hcpp::u64>(entry);
            for (int i = 0; i < 6; ++i) {
                --t;
                *t = 0;
            }
            return reinterpret_cast<hcpp::u64>(t);
        }
    }  // namespace
    void scheduler_initialize() noexcept {
        for (int i = 0; i < thread_count; ++i) workers[i] = {prepare(stacks[i], worker_entry), 0, false};
        current = 0;
        switches = 0;
        initialized = true;
    }
    bool scheduler_selftest() noexcept {
        if (!initialized) scheduler_initialize();
        bool pending = true;
        while (pending) {
            pending = false;
            for (int i = 0; i < thread_count; ++i)
                if (!workers[i].done) {
                    pending = true;
                    current = i;
                    hype_context_switch(&main_rsp, workers[i].rsp);
                }
        }
        return workers[0].iterations == 3 && workers[1].iterations == 3;
    }
    hcpp::size_t scheduler_thread_count() noexcept { return thread_count; }
    hcpp::u32 scheduler_context_switches() noexcept { return switches; }
}  // namespace hype::sched
