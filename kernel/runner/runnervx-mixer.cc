#include "kernel/runner/runnervx-mixer.h"

namespace hype::vx {

    void vxmixer_init(VxMixer& mixer) noexcept {
        for (hcpp::size_t q = 0; q < vx_queue_count; ++q) {
            mixer.queues[q].head = 0;
            mixer.queues[q].tail = 0;
            mixer.queues[q].mask = vx_queue_capacity - 1;
            mixer.queues[q].reserved = 0;
            mixer.queues[q].slots = mixer.storage[q];
        }
        mixer.next = 0;
        mixer.pops = 0;
    }

    bool vxmixer_push(VxMixer& mixer, hcpp::u32 queue,
                      hcpp::u32 value) noexcept {
        if (queue >= vx_queue_count) {
            return false;
        }
        return runnervx_ring_push(&mixer.queues[queue], value) != 0;
    }

    hcpp::u32 vxmixer_drain(VxMixer& mixer, hcpp::u32* out,
                            hcpp::u32 out_max) noexcept {
        hcpp::u32 popped = 0;
        for (hcpp::u32 lane = 0; lane < vx_queue_count && popped < out_max;
             ++lane) {
            const hcpp::u32 q =
                (mixer.next + lane) % static_cast<hcpp::u32>(vx_queue_count);
            for (hcpp::u32 n = 0; n < vx_queue_quota && popped < out_max; ++n) {
                hcpp::u32 value = 0;
                if (runnervx_ring_pop(&mixer.queues[q], &value) == 0) {
                    break;
                }
                out[popped++] = value;
                ++mixer.pops;
            }
        }
        mixer.next = (mixer.next + 1) % static_cast<hcpp::u32>(vx_queue_count);
        return popped;
    }

    hcpp::u32 vxmixer_pending(const VxMixer& mixer) noexcept {
        hcpp::u32 pending = 0;
        for (hcpp::size_t q = 0; q < vx_queue_count; ++q) {
            pending += mixer.queues[q].tail - mixer.queues[q].head;
        }
        return pending;
    }

}  // namespace hype::vx
