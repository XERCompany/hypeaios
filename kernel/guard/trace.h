#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"

namespace hype::guard {

    // Guard trace — the debug channel for the security module. Every
    // guard_trace() call appends a line to a fixed ring AND mirrors it
    // to serial (when the mirror is on), so boot markers like
    // "guard check=fs_write deny" survive in both places: serial for
    // the boot log, the ring for post-mortem dumps from a UI/console.
    //
    // Levels follow klog convention; trace_min_level() filters the
    // noisy tiers when the ring must stay focused on denials.

    enum class GuardTraceLevel : hcpp::u8 {
        Debug = 0,
        Info = 1,
        Warn = 2,
        Err = 3,
    };

    constexpr hcpp::u32 guard_trace_capacity = 64;
    constexpr hcpp::u32 guard_trace_msg_size = 96;

    struct GuardTraceEntry {
        GuardTraceLevel level;
        char msg[guard_trace_msg_size];
        hcpp::u64 timestamp_ms;
    };

    void guard_trace(GuardTraceLevel level, const char* msg) noexcept;
    void guard_trace(const char* msg) noexcept;  // Info-level shortcut
    void guard_trace_set_serial(bool enabled) noexcept;
    void guard_trace_set_min_level(GuardTraceLevel level) noexcept;
    hcpp::u32 guard_trace_count() noexcept;
    using GuardTraceCb = void (*)(const GuardTraceEntry& entry,
                                  void* ctx) noexcept;
    void guard_trace_dump(GuardTraceCb cb, void* ctx) noexcept;
    void guard_trace_clear() noexcept;

}  // namespace hype::guard
