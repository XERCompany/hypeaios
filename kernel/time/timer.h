#pragma once

#include "hcpp/types.hpp"

namespace hype::time {

    enum class TimeSource : hcpp::u8 { Pit = 0, Hpet = 1, Tsc = 2 };

    // Initializes the timer ladder: HPET discovery + counter start, then
    // TSC calibration against the best reference available, then source
    // selection. Prints the boot serial markers "hpet dev=...",
    // "tsc khz=... cal=..." and "time src=...". Requires acpi::initialize()
    // to have run; PIT-tick calibration additionally needs interrupts on.
    // Always succeeds in the sense that the PIT tick path remains usable.
    bool timer_initialize() noexcept;

    TimeSource time_source() noexcept;
    const char *time_source_name() noexcept;

    // Monotonic uptime in nanoseconds from the best available source:
    // calibrated TSC, else the HPET main counter, else PIT ticks at 10ms
    // granularity.
    hcpp::u64 time_now_ns() noexcept;
    hcpp::u64 uptime_ns() noexcept;
    hcpp::u64 uptime_ms() noexcept;

}  // namespace hype::time
