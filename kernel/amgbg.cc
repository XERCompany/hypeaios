#include "kernel/amgbg.h"
namespace hype::tasks {
    namespace {
        hcpp::u32 active;
        hcpp::u32 sequence;
    }  // namespace
    void background_reset() noexcept {
        active = 0;
        sequence = 0;
    }
    hcpp::u32 background_begin() noexcept {
        ++active;
        return ++sequence;
    }
    void background_finish(hcpp::u32 token) noexcept {
        if (token != 0 && active != 0) --active;
    }
    hcpp::u32 background_active() noexcept { return active; }
}  // namespace hype::tasks

namespace hype::tasks {
    bool background_busy() noexcept { return active != 0; }
}  // namespace hype::tasks
