#pragma once
#include "hcpp/types.hpp"
namespace hype::sched {
    void scheduler_initialize() noexcept;
    bool scheduler_selftest() noexcept;
    hcpp::size_t scheduler_thread_count() noexcept;
    hcpp::u32 scheduler_context_switches() noexcept;
}  // namespace hype::sched
