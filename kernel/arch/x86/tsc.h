#pragma once

#include "hcpp/types.hpp"

namespace hype::x86 {

    // Raw rdtsc cycle count.
    hcpp::u64 tsc_read() noexcept;

    // Calibrates the TSC frequency. Reference window preference order:
    // HPET main counter (~20ms), PIT channel 2 one-shot (~10ms, no
    // interrupts needed), PIT 100 Hz tick count (~50ms, needs interrupts
    // on). CPUID leaf 0x16 base MHz is read first and used both as a
    // sanity bound on the measurement and as the fallback frequency.
    // Safe to call repeatedly; cheap once calibrated.
    bool tsc_calibrate() noexcept;

    bool tsc_calibrated() noexcept;

    // Calibrated frequency in kHz (0 when not calibrated).
    hcpp::u64 tsc_khz() noexcept;

    // Nanoseconds since the TSC epoch (captured at calibration).
    hcpp::u64 tsc_now_ns() noexcept;

}  // namespace hype::x86
