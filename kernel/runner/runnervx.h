#pragma once

#include "hcpp/types.hpp"
#include "kernel/runner/runnervx-command.h"
#include "kernel/runner/runnervx-mixer.h"

// RunnerVX (issue #115): the v-executor — a tick-driven job/command
// runner. runnervx_post() drops a VxCommand into the priority lane's
// ring, runnervx_pump() (called from the idle loop next to net_poll)
// drains the lanes through the round-robin mixer and executes each
// command, writing back a VxStatus and bumping the stats.
namespace hype::vx {

    constexpr hcpp::size_t vx_pool_capacity = 64;
    constexpr hcpp::u32 vx_invalid_ticket = 0xFFFFFFFFu;
    constexpr hcpp::u32 vx_pump_max = vx_queue_quota * vx_queue_count;

    struct VxStats {
        hcpp::u32 posted;
        hcpp::u32 executed;    // Done
        hcpp::u32 failed;      // executed but reported Failed
        hcpp::u32 dropped;     // post on a full pool/queue
        hcpp::u32 pending;     // queued right now
    };

    // Clears the pool, lanes and stats. Returns true (no hardware to
    // fail); the caller prints "runnervx core=ok".
    bool runnervx_init() noexcept;

    // Copies the command into a free pool slot and queues it on the
    // command's priority lane. Returns the ticket (usable with
    // runnervx_status) or vx_invalid_ticket when dropped.
    hcpp::u32 runnervx_post(const VxCommand& cmd) noexcept;

    // One pump: mixer-drains the lanes (per-queue quota) and executes
    // the popped commands. Returns commands executed this call.
    hcpp::u32 runnervx_pump() noexcept;

    // Completion status for a live ticket; VxStatus::Dropped for an
    // unknown or already-recycled ticket.
    VxStatus runnervx_status(hcpp::u32 ticket) noexcept;
    VxStats runnervx_stats() noexcept;

    // Posts 3 commands (nop, atomic add, callback), pumps, and verifies
    // completions plus side effects. No hardware required.
    bool runnervx_selftest() noexcept;

}  // namespace hype::vx
