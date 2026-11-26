#pragma once

#include "hcpp/types.hpp"
#include "kernel/acpi/acpi.h"

namespace hype::acpi {

    enum class MadtEntryType : hcpp::u8 {
        LocalApic = 0,
        IoApic = 1,
        InterruptSourceOverride = 2,
        NmiSource = 3,
        LocalApicNmi = 4,
        LocalApicAddressOverride = 5,
        X2Apic = 9,
    };

    struct MadtInfo {
        bool present;
        hcpp::u32 legacy_lapic_address;
        bool pc_at_compatible;
        hcpp::size_t local_apic_count;
        hcpp::size_t enabled_cpu_count;
        hcpp::size_t ioapic_count;
        hcpp::size_t interrupt_override_count;
        hcpp::uintptr_t first_ioapic_address;
        hcpp::uintptr_t effective_lapic_address;
    };

    void madt_initialize() noexcept;
    const MadtInfo& madt_info() noexcept;

}  // namespace hype::acpi
