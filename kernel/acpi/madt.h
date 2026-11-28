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

    // Interrupt Source Override entry: remaps a legacy ISA interrupt
    // source onto a (possibly different) GSI with its own polarity and
    // trigger mode. `flags` is the raw MADT field: bits [1:0] polarity
    // (0 = bus default, 1 = active high, 3 = active low), bits [3:2]
    // trigger (0 = bus default, 1 = edge, 3 = level).
    struct MadtIso {
        hcpp::u8 bus;
        hcpp::u8 source_irq;
        hcpp::u32 gsi;
        hcpp::u16 flags;
    };

    constexpr hcpp::size_t madt_max_isos = 16;
    constexpr hcpp::size_t madt_max_cpus = 32;

    // One MADT Processor Local APIC entry (type 0): ACPI processor uid,
    // the LAPIC id INIT-SIPI-SIPI targets, and the enabled bit from the
    // MADT flags field (bit 0). Entries are recorded even when disabled
    // so firmware's full topology stays visible; `enabled` gates bring-up.
    struct MadtCpu {
        hcpp::u8 acpi_uid;
        hcpp::u8 lapic_id;
        bool enabled;
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
        MadtCpu cpus[madt_max_cpus];
        hcpp::size_t cpu_list_count;
        MadtIso iso_overrides[madt_max_isos];
        hcpp::size_t iso_override_count;
    };

    void madt_initialize() noexcept;
    const MadtInfo& madt_info() noexcept;

}  // namespace hype::acpi
