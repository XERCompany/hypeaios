#pragma once

#include "hcpp/types.hpp"

namespace hype::time {

    // Locates the ACPI HPET table (signature "HPET"), validates the MMIO
    // base (generic address structure with address-space-id 0 = system
    // memory) and starts the main counter free-running for timestamps.
    // The legacy-replacement route (general config bit 1) is deliberately
    // left off: IRQ0 keeps running on the PIT and IOAPIC routing lands
    // separately. Safe to call repeatedly; cheap after the first call.
    bool hpet_initialize() noexcept;

    // True once the main counter has been verified to advance.
    bool hpet_ready() noexcept;

    // Counter frequency derived from the femtosecond clock period in the
    // general capabilities register (0 when not initialized).
    hcpp::u64 hpet_freq_hz() noexcept;

    // Monotonic nanoseconds read off the main counter. Returns 0 when the
    // HPET is not initialized.
    hcpp::u64 hpet_now_ns() noexcept;

    // Busy-waits on the raw main counter. Used as the reference window for
    // TSC calibration before any calibrated time source exists.
    void hpet_wait_ns(hcpp::u64 ns) noexcept;

}  // namespace hype::time
