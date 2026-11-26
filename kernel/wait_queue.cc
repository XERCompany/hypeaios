#include "kernel/wait_queue.h"
namespace hype::kernel {
    void wait_queue_initialize(WaitQueue& queue) noexcept { queue = {}; }
    bool wait_queue_add(WaitQueue& queue, hcpp::u32 id) noexcept {
        if (id == 0 || queue.count >= 16) return false;
        queue.waiting[queue.count++] = id;
        return true;
    }
    bool wait_queue_remove(WaitQueue& queue, hcpp::u32 id) noexcept {
        for (hcpp::size_t i = 0; i < queue.count; ++i)
            if (queue.waiting[i] == id) {
                queue.waiting[i] = queue.waiting[--queue.count];
                return true;
            }
        return false;
    }
    hcpp::u32 wait_queue_wake_one(WaitQueue& queue) noexcept {
        if (queue.count == 0) return 0;
        const hcpp::u32 id = queue.waiting[0];
        wait_queue_remove(queue, id);
        return id;
    }
    hcpp::size_t wait_queue_wake_all(WaitQueue& queue) noexcept {
        const hcpp::size_t n = queue.count;
        queue.count = 0;
        return n;
    }
}  // namespace hype::kernel
