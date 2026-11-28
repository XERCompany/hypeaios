#pragma once

#include "hcpp/types.hpp"

// RunnerVX mixer/arbiter (issue #115): four bounded u32 rings (one per
// VxPriority lane) drained round-robin with a per-queue quota on every
// pump. The rings are monotonic-cursor queues; push/pop live in
// kernel/runner/runnervx.S as cli + lock-bts-guarded helpers, so posts
// are safe against IRQ-time producers and SMP peers.
namespace hype::vx {

    // Layout is consumed by runnervx.S — keep offsets stable:
    //   +0  head   (u32, pop cursor, monotonic)
    //   +4  tail   (u32, push cursor, monotonic)
    //   +8  mask   (u32, capacity - 1, power of two)
    //   +12 reserved (used by runnervx.S as the ring lock word)
    //   +16 slots  (u32*)
    struct VxRing {
        volatile hcpp::u32 head;
        volatile hcpp::u32 tail;
        hcpp::u32 mask;
        hcpp::u32 reserved;
        hcpp::u32* slots;
    };

    constexpr hcpp::size_t vx_queue_count = 4;
    constexpr hcpp::u32 vx_queue_capacity = 32;   // power of two
    constexpr hcpp::u32 vx_queue_quota = 8;       // max pops per queue per pump

    struct VxMixer {
        VxRing queues[vx_queue_count];
        hcpp::u32 storage[vx_queue_count][vx_queue_capacity];
        hcpp::u32 next;   // round-robin start lane
        hcpp::u64 pops;   // total drained since init
    };

    void vxmixer_init(VxMixer& mixer) noexcept;
    bool vxmixer_push(VxMixer& mixer, hcpp::u32 queue, hcpp::u32 value) noexcept;

    // Round-robin drain: starts at lane mixer.next, pops up to
    // vx_queue_quota entries per lane into out[] (max out_max total),
    // advances the robin. Returns entries popped.
    hcpp::u32 vxmixer_drain(VxMixer& mixer, hcpp::u32* out,
                            hcpp::u32 out_max) noexcept;
    hcpp::u32 vxmixer_pending(const VxMixer& mixer) noexcept;

    // NASM helpers (kernel/runner/runnervx.S). Both return 1 on success,
    // 0 on full/empty.
    extern "C" hcpp::u32 runnervx_ring_push(VxRing* ring,
                                            hcpp::u32 value) noexcept;
    extern "C" hcpp::u32 runnervx_ring_pop(VxRing* ring,
                                           hcpp::u32* out) noexcept;

}  // namespace hype::vx
