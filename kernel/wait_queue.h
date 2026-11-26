#pragma once
#include "hcpp/types.hpp"
namespace hype::kernel {
    struct WaitQueue {
        hcpp::u32 waiting[16];
        hcpp::size_t count;
    };
    void wait_queue_initialize(WaitQueue& queue) noexcept;
    bool wait_queue_add(WaitQueue& queue, hcpp::u32 task_id) noexcept;
    bool wait_queue_remove(WaitQueue& queue, hcpp::u32 task_id) noexcept;
    hcpp::u32 wait_queue_wake_one(WaitQueue& queue) noexcept;
    hcpp::size_t wait_queue_wake_all(WaitQueue& queue) noexcept;
}  // namespace hype::kernel
