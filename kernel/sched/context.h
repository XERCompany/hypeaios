#pragma once
#include "hcpp/types.hpp"
namespace hype::sched {
    using ThreadEntry = void (*)() noexcept;
    void context_initialize() noexcept;
    bool context_selftest() noexcept;
    bool context_ready() noexcept;
}  // namespace hype::sched
