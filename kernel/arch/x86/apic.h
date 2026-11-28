#pragma once

#include "hcpp/types.hpp"

namespace hype::x86 {

    struct ApicState {
        bool supported;         // CPUID reports an on-chip APIC
        bool madt_present;      // MADT supplied LAPIC/IOAPIC addresses
        bool ioapic_present;
        bool active;            // handoff complete: 8259 masked, LAPIC timer ticking
        hcpp::uintptr_t lapic_base;
        hcpp::uintptr_t ioapic_base;
        hcpp::u32 ioapic_entries;       // IOREDTBL size (max redirection + 1)
        hcpp::u32 lapic_timer_count;    // initial-count value giving the 100 Hz tick
        hcpp::u64 ticks_observed;       // LAPIC timer ticks counted during the selftest
    };

    // Tries to replace the 8259 PIC interrupt path with LAPIC + IOAPIC.
    // Requires the IDT loaded, the MADT parsed, and the legacy path
    // already running (PIT ticking through the PIC, interrupts enabled)
    // because the PIT tick is the LAPIC timer calibration reference.
    // On success the 8259 pair is fully masked, the LAPIC timer supplies
    // the 100 Hz tick on vector 32 and true is returned. On any failure
    // the PIC path is left running untouched and false is returned.
    bool apic_initialize() noexcept;

    const ApicState& apic_state() noexcept;
    bool apic_active() noexcept;

    // End-of-interrupt for vectors 32..47. Sends a LAPIC EOI while the
    // APIC path is live and a legacy 8259 EOI otherwise, so IRQ handlers
    // never need to know which controller delivered the interrupt.
    void apic_send_eoi(hcpp::u8 irq) noexcept;

    // Opens a legacy IRQ line on the IOAPIC (clears the mask bit of the
    // redirection entry programmed by apic_initialize). IRQ0 stays
    // masked on the APIC path — the LAPIC timer already supplies vector
    // 32 — and IRQ2 is a no-op: the slave cascade exists only inside
    // the 8259 pair. Safe to call with irq >= 16 (ignored).
    void apic_unmask_irq(hcpp::u8 irq) noexcept;

    // IDT vector programmed for a GSI, or 0 when unrouted.
    hcpp::u8 apic_gsi_vector(hcpp::u32 gsi) noexcept;

    // BSP LAPIC id (physical destination for MSI/MSI-X messages), or 0
    // when the LAPIC MMIO window has not been mapped.
    hcpp::u32 apic_lapic_id() noexcept;

    // End-of-interrupt for message-signalled vectors (48..255): writes
    // the LAPIC EOI register whenever the LAPIC is mapped. Unlike
    // apic_send_eoi() it never falls back to the 8259 — MSI delivery
    // does not involve the PIC at all.
    void apic_lapic_eoi() noexcept;

}  // namespace hype::x86
