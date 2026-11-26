#include "kernel/amtfg.h"
namespace hype::tasks {
    namespace {
        hcpp::u32 active;
        hcpp::u32 sequence;
    }  // namespace
    void foreground_reset() noexcept {
        active = 0;
        sequence = 0;
    }
    hcpp::u32 foreground_begin() noexcept {
        ++active;
        return ++sequence;
    }
    void foreground_finish(hcpp::u32 token) noexcept {
        if (token != 0 && active != 0) --active;
    }
    hcpp::u32 foreground_active() noexcept { return active; }
}  // namespace hype::tasks
