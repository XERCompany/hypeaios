#pragma once

#include "hcpp/types.hpp"

namespace hype::x86 {

    constexpr hcpp::size_t smp_max_cpus = 16;

    // Per-CPU bookkeeping. APs write `online`, `counter` and `parked`
    // with relaxed atomics; the BSP reads them from the bring-up path.
    // `stack_top` is the PMM-allocated stack the trampoline loads into
    // RSP before calling the C entry.
    struct SmpCpu {
        hcpp::u32 lapic_id;        // physical LAPIC id (IPI destination)
        hcpp::u32 acpi_uid;        // MADT processor uid
        volatile hcpp::u32 online; // 1 once the AP reached its C entry
        volatile hcpp::u32 parked; // 1 once the AP sits in its hlt loop
        hcpp::u32 is_bsp;
        hcpp::u32 reserved0;
        hcpp::uintptr_t stack_base;
        hcpp::uintptr_t stack_top;
        volatile hcpp::u64 counter;   // free-running AP progress counter
    };

    // Enumerates CPUs from the MADT and starts every enabled
    // Application Processor via LAPIC INIT-SIPI-SIPI. Requires the
    // LAPIC MMIO path live (apic_initialize succeeded), the MADT
    // parsed, the PMM up (per-AP stacks) and a working uptime_ns()
    // for the inter-IPI delays and the bounded started-flag wait.
    // Always safe to call on a 1-CPU machine: prints "smp cpus=1"
    // and returns false.
    bool smp_initialize() noexcept;

    // CPUs enumerated from the MADT (BSP included).
    hcpp::size_t smp_cpu_count() noexcept;

    // Index of the calling CPU in the SMP table, resolved through its
    // LAPIC id. Returns 0 (BSP) before smp_initialize() ran.
    hcpp::size_t smp_cpu_id() noexcept;

    // True when executing on the bootstrap processor — also true
    // before SMP bring-up, so the single-CPU path needs no guard.
    bool smp_is_bsp() noexcept;

    // CPUs that completed INIT-SIPI-SIPI and reached their C entry.
    hcpp::u32 smp_online_count() noexcept;

    const SmpCpu* smp_cpu(hcpp::size_t index) noexcept;

}  // namespace hype::x86
