#include "kernel/sched/context.h"
namespace hype::sched {
    namespace {
        constexpr hcpp::size_t stack_size = 4096;
        alignas(16) hcpp::u8 secondary_stack[stack_size];
        hcpp::u64 main_rsp;
        hcpp::u64 secondary_rsp;
        volatile bool secondary_reached;
        volatile bool returned_to_main;
        extern "C" void hype_context_switch(hcpp::u64* save_rsp, hcpp::u64 restore_rsp) noexcept;
        void secondary_entry() noexcept {
            secondary_reached = true;
            hype_context_switch(&secondary_rsp, main_rsp);
            returned_to_main = true;
        }
        hcpp::u64 prepare_stack(void* stack, hcpp::size_t size, ThreadEntry entry) noexcept {
            auto* top = reinterpret_cast<hcpp::u64*>(reinterpret_cast<hcpp::u8*>(stack) + size);
            --top;
            *top = reinterpret_cast<hcpp::u64>(entry);
            for (int i = 0; i < 6; ++i) {
                --top;
                *top = 0;
            }
            return reinterpret_cast<hcpp::u64>(top);
        }
    }  // namespace
    void context_initialize() noexcept {
        secondary_reached = false;
        returned_to_main = false;
        secondary_rsp = prepare_stack(secondary_stack, stack_size, secondary_entry);
    }
    bool context_selftest() noexcept {
        context_initialize();
        hype_context_switch(&main_rsp, secondary_rsp);
        return secondary_reached;
    }
    bool context_ready() noexcept { return secondary_reached; }
}  // namespace hype::sched
