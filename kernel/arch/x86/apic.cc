#include "kernel/arch/x86/apic.h"

#include "kernel/acpi/madt.h"
#include "kernel/arch/x86/io.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"

// LAPIC (xAPIC, MMIO) + IOAPIC bring-up that replaces the 8259 PIC
// interrupt path. Design notes:
//
//  * The legacy IRQ numbers keep their IDT vectors (IRQn -> vector
//    32+n), so interrupt_handle() and the irq_dispatch table need no
//    vector remapping at all — only the delivery engine changes.
//  * The LAPIC timer (periodic, vector 32, divide by 16) replaces the
//    PIT IRQ0 tick. The PIT stays running as hardware but its line is
//    kept masked in the IOAPIC so vector 32 is not delivered twice.
//  * apic_initialize() only masks the 8259 pair *after* the LAPIC is
//    set up and the IOAPIC entries are programmed; a bounded selftest
//    then counts delivered timer ticks. If it fails, the PIC masks are
//    restored and the legacy path keeps working.

namespace hype::x86 {
    namespace {
        // xAPIC register offsets from the LAPIC base.
        constexpr hcpp::u32 lapic_reg_id = 0x20;
        constexpr hcpp::u32 lapic_reg_version = 0x30;
        constexpr hcpp::u32 lapic_reg_tpr = 0x80;
        constexpr hcpp::u32 lapic_reg_eoi = 0xB0;
        constexpr hcpp::u32 lapic_reg_dfr = 0xE0;
        constexpr hcpp::u32 lapic_reg_svr = 0xF0;
        constexpr hcpp::u32 lapic_reg_lvt_timer = 0x320;
        constexpr hcpp::u32 lapic_reg_timer_init = 0x380;
        constexpr hcpp::u32 lapic_reg_timer_cur = 0x390;
        constexpr hcpp::u32 lapic_reg_timer_div = 0x3E0;

        constexpr hcpp::u32 lvt_masked = 1u << 16;
        constexpr hcpp::u32 lvt_periodic = 1u << 17;
        constexpr hcpp::u32 timer_div_16 = 0x3;  // divide configuration: /16
        constexpr hcpp::u32 svr_software_enable = 1u << 8;
        constexpr hcpp::u8 spurious_vector = 0xFF;
        constexpr hcpp::u8 timer_vector = 32;

        // IOAPIC register-select window.
        constexpr hcpp::u8 ioapic_reg_ver = 0x01;
        constexpr hcpp::u8 ioapic_reg_redtbl = 0x10;
        constexpr hcpp::u32 redir_masked = 1u << 16;
        constexpr hcpp::u32 redir_level = 1u << 15;
        constexpr hcpp::u32 redir_low_active = 1u << 13;

        constexpr hcpp::u32 msr_ia32_apic_base = 0x1B;
        constexpr hcpp::u64 apic_base_enable = 1ull << 11;
        constexpr hcpp::u64 apic_base_x2 = 1ull << 10;

        constexpr hcpp::size_t max_tracked_gsis = 64;
        // Selftest: wait for ~100 timer ticks (~1 s) with a bounded spin
        // so a dead LAPIC timer can never hang boot.
        constexpr hcpp::u64 selftest_ticks_target = 100;
        constexpr hcpp::u64 selftest_ticks_minimum = 10;
        constexpr hcpp::u64 selftest_spin_bound = 400000000ull;

        ApicState state{};
        volatile hcpp::u32* lapic = nullptr;
        volatile hcpp::u32* ioapic_sel = nullptr;
        volatile hcpp::u32* ioapic_win = nullptr;
        hcpp::u8 gsi_vector[max_tracked_gsis];

        void cpuid(hcpp::u32 leaf, hcpp::u32& eax, hcpp::u32& ebx, hcpp::u32& ecx, hcpp::u32& edx) noexcept {
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0));
        }
        hcpp::u64 read_msr(hcpp::u32 msr) noexcept {
            hcpp::u32 low, high;
            asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
            return (static_cast<hcpp::u64>(high) << 32) | low;
        }
        void write_msr(hcpp::u32 msr, hcpp::u64 value) noexcept {
            asm volatile("wrmsr" ::"a"(static_cast<hcpp::u32>(value)),
                         "d"(static_cast<hcpp::u32>(value >> 32)), "c"(msr));
        }

        hcpp::u32 lapic_read(hcpp::u32 reg) noexcept { return lapic[reg / 4]; }
        void lapic_write(hcpp::u32 reg, hcpp::u32 value) noexcept {
            lapic[reg / 4] = value;
            (void)lapic_read(lapic_reg_id);  // posted-write flush
        }
        hcpp::u32 ioapic_read(hcpp::u8 reg) noexcept {
            *ioapic_sel = reg;
            return *ioapic_win;
        }
        void ioapic_write(hcpp::u8 reg, hcpp::u32 value) noexcept {
            *ioapic_sel = reg;
            *ioapic_win = value;
        }
        // IOREDTBL is {low, high} dword pairs at indices 0x10+2i, 0x11+2i.
        void ioapic_redir_write(hcpp::u32 index, hcpp::u64 entry) noexcept {
            ioapic_write(static_cast<hcpp::u8>(ioapic_reg_redtbl + index * 2 + 1),
                         static_cast<hcpp::u32>(entry >> 32));
            ioapic_write(static_cast<hcpp::u8>(ioapic_reg_redtbl + index * 2),
                         static_cast<hcpp::u32>(entry));
        }
        hcpp::u64 ioapic_redir_read(hcpp::u32 index) noexcept {
            const hcpp::u64 low = ioapic_read(static_cast<hcpp::u8>(ioapic_reg_redtbl + index * 2));
            const hcpp::u64 high = ioapic_read(static_cast<hcpp::u8>(ioapic_reg_redtbl + index * 2 + 1));
            return (high << 32) | low;
        }

        // Resolves a legacy IRQ line to the GSI it actually arrives on,
        // honouring MADT Interrupt Source Overrides. Flags out-param gets
        // the raw MADT polarity/trigger field (0 when there is no ISO).
        hcpp::u32 irq_gsi(hcpp::u8 irq, hcpp::u16& flags) noexcept {
            const auto& madt = acpi::madt_info();
            for (hcpp::size_t i = 0; i < madt.iso_override_count; ++i) {
                const auto& iso = madt.iso_overrides[i];
                if (iso.bus == 0 && iso.source_irq == irq) {
                    flags = iso.flags;
                    return iso.gsi;
                }
            }
            flags = 0;
            return irq;
        }

        // Programs (masked) the IOREDTBL entry for one legacy IRQ:
        // vector = 32 + irq, physical destination = BSP LAPIC id,
        // polarity/trigger from the MADT ISO when present (ISA default
        // is edge-triggered, active-high otherwise).
        void ioapic_route_irq(hcpp::u8 irq, hcpp::u32 lapic_id) noexcept {
            hcpp::u16 iso_flags;
            const hcpp::u32 gsi = irq_gsi(irq, iso_flags);
            if (gsi >= state.ioapic_entries) return;
            hcpp::u32 low = redir_masked | static_cast<hcpp::u32>(32 + irq);
            if ((iso_flags & 0x3) == 0x3) low |= redir_low_active;
            if (((iso_flags >> 2) & 0x3) == 0x3) low |= redir_level;
            ioapic_redir_write(gsi, (static_cast<hcpp::u64>(lapic_id) << 56) | low);
            if (gsi < max_tracked_gsis) gsi_vector[gsi] = static_cast<hcpp::u8>(32 + irq);
        }

        bool lapic_probe() noexcept {
            const hcpp::u32 version = lapic_read(lapic_reg_version);
            if (version == 0 || version == 0xFFFFFFFFu) return false;
            state.lapic_base = reinterpret_cast<hcpp::uintptr_t>(lapic);
            return true;
        }

        // Measures how many LAPIC timer counts (divide-by-16) elapse per
        // PIT tick while the PIC path still delivers the 100 Hz IRQ0.
        bool lapic_timer_calibrate() noexcept {
            lapic_write(lapic_reg_timer_div, timer_div_16);
            lapic_write(lapic_reg_lvt_timer, lvt_masked);  // one-shot, no delivery
            const hcpp::u64 before = interrupts::pit_ticks();
            lapic_write(lapic_reg_timer_init, 0xFFFFFFFFu);
            interrupts::pit_wait_ticks(2);
            const hcpp::u64 waited = interrupts::pit_ticks() - before;
            const hcpp::u32 elapsed = 0xFFFFFFFFu - lapic_read(lapic_reg_timer_cur);
            lapic_write(lapic_reg_timer_init, 0);
            if (waited == 0 || elapsed == 0) return false;
            state.lapic_timer_count = static_cast<hcpp::u32>(elapsed / waited);
            return state.lapic_timer_count != 0;
        }

        void lapic_timer_start() noexcept {
            lapic_write(lapic_reg_timer_div, timer_div_16);
            lapic_write(lapic_reg_lvt_timer, timer_vector | lvt_periodic);
            lapic_write(lapic_reg_timer_init, state.lapic_timer_count);
        }
        void lapic_timer_stop() noexcept {
            lapic_write(lapic_reg_lvt_timer, lvt_masked);
            lapic_write(lapic_reg_timer_init, 0);
        }
    }  // namespace

    bool apic_initialize() noexcept {
        state = {};
        for (auto& v : gsi_vector) v = 0;

        hcpp::u32 eax, ebx, ecx, edx;
        cpuid(1, eax, ebx, ecx, edx);
        state.supported = (edx & (1u << 9)) != 0;
        if (!state.supported) {
            serial::write_line("apic cpu=noapic");
            return false;
        }
        hcpp::u64 apic_base_msr = read_msr(msr_ia32_apic_base);
        if ((apic_base_msr & apic_base_x2) != 0) {
            // x2APIC mode disables the xAPIC MMIO page this driver uses.
            serial::write_line("apic x2=skip");
            return false;
        }
        const auto& madt = acpi::madt_info();
        state.madt_present = madt.present;
        if (!madt.present || madt.effective_lapic_address == 0) {
            serial::write_line("apic madt=missing");
            return false;
        }
        state.ioapic_present = madt.ioapic_count != 0 && madt.first_ioapic_address != 0;
        if (!state.ioapic_present) {
            serial::write_line("apic io=missing");
            return false;
        }
        // The boot page tables identity-map the low 4 GiB, so the MMIO
        // windows must live below 4 GiB (they always do on PC hardware).
        if (madt.effective_lapic_address >= 0x100000000ull ||
            madt.first_ioapic_address >= 0x100000000ull) {
            serial::write_line("apic mmio=high");
            return false;
        }

        // --- local APIC ---
        if ((apic_base_msr & apic_base_enable) == 0) {
            apic_base_msr |= apic_base_enable;
            write_msr(msr_ia32_apic_base, apic_base_msr);
        }
        lapic = reinterpret_cast<volatile hcpp::u32*>(madt.effective_lapic_address);
        if (!lapic_probe()) {
            serial::write_line("apic lapic=dead");
            return false;
        }
        lapic_write(lapic_reg_dfr, 0xFFFFFFFFu);                       // flat model
        lapic_write(lapic_reg_tpr, 0);                                 // accept all priorities
        lapic_write(lapic_reg_svr, lapic_read(lapic_reg_svr) | svr_software_enable | spurious_vector);
        const hcpp::u32 lapic_id = lapic_read(lapic_reg_id) >> 24;

        // --- IOAPIC ---
        ioapic_sel = reinterpret_cast<volatile hcpp::u32*>(madt.first_ioapic_address);
        ioapic_win = reinterpret_cast<volatile hcpp::u32*>(madt.first_ioapic_address + 0x10);
        state.ioapic_base = madt.first_ioapic_address;
        const hcpp::u32 ver = ioapic_read(ioapic_reg_ver);
        if (ver == 0xFFFFFFFFu) {
            serial::write_line("apic io=dead");
            return false;
        }
        state.ioapic_entries = ((ver >> 16) & 0xFF) + 1;
        // Silence every pin, then route the 16 legacy lines (masked) at
        // vectors 32..47 so IRQ handlers keep their vector->irq mapping.
        for (hcpp::u32 i = 0; i < state.ioapic_entries; ++i)
            ioapic_redir_write(i, redir_masked);
        for (hcpp::u8 irq = 0; irq < 16; ++irq) ioapic_route_irq(irq, lapic_id);
        // Verify the routing table is writable/readable (IOAPIC live).
        // Compare against what ioapic_route_irq() programmed for IRQ1,
        // including any polarity/trigger bits from its ISO.
        hcpp::u16 iso1;
        const hcpp::u32 gsi1 = irq_gsi(1, iso1);
        if (gsi1 < state.ioapic_entries) {
            hcpp::u32 expected_low = redir_masked | 33u;
            if ((iso1 & 0x3) == 0x3) expected_low |= redir_low_active;
            if (((iso1 >> 2) & 0x3) == 0x3) expected_low |= redir_level;
            const hcpp::u64 readback = ioapic_redir_read(gsi1);
            if ((readback & 0x1AFFFu) != (expected_low & 0x1AFFFu)) {
                serial::write_line("apic io=readonly");
                return false;
            }
        }
        serial::write_line("apic lapic=ok io=ok");

        // --- calibrate the LAPIC timer against the still-running PIT ---
        if (!lapic_timer_calibrate()) {
            serial::write_line("apic timer=uncalibrated");
            return false;
        }

        // --- handoff ---
        const hcpp::u8 pic_master_mask = hype_port_in8(0x21);
        const hcpp::u8 pic_slave_mask = hype_port_in8(0xA1);
        interrupts::pic_mask_all();
        // The IOAPIC is verified live; from here on EOI goes to the
        // LAPIC. Order matters: any PIC IRQ still in flight before the
        // mask landed gets a proper 8259 EOI.
        state.active = true;
        lapic_timer_start();
        const hcpp::u64 start = interrupts::pit_ticks();
        hcpp::u64 guard = 0;
        while (interrupts::pit_ticks() - start < selftest_ticks_target && guard++ < selftest_spin_bound)
            asm volatile("pause");
        state.ticks_observed = interrupts::pit_ticks() - start;
        serial::write("apic ticks=");
        serial::write_dec32(static_cast<hcpp::u32>(state.ticks_observed));
        serial::write("\r\n");
        if (state.ticks_observed < selftest_ticks_minimum) {
            // The LAPIC timer never delivered vector 32: restore the PIC
            // exactly as we found it so the legacy path keeps running.
            lapic_timer_stop();
            state.active = false;
            hype_port_out8(0x21, pic_master_mask);
            hype_port_out8(0xA1, pic_slave_mask);
            serial::write_line("apic fallback=pic");
            return false;
        }
        return true;
    }

    const ApicState& apic_state() noexcept { return state; }
    bool apic_active() noexcept { return state.active; }

    void apic_send_eoi(hcpp::u8 irq) noexcept {
        if (state.active && lapic != nullptr) {
            lapic[lapic_reg_eoi / 4] = 0;
        } else {
            interrupts::pic_send_eoi(irq);
        }
    }

    void apic_unmask_irq(hcpp::u8 irq) noexcept {
        if (!state.active || irq >= 16 || ioapic_win == nullptr) return;
        if (irq == 0 || irq == 2) return;  // LAPIC timer owns 32; no cascade on IOAPIC
        hcpp::u16 iso_flags;
        const hcpp::u32 gsi = irq_gsi(irq, iso_flags);
        if (gsi >= state.ioapic_entries) return;
        const hcpp::u64 entry = ioapic_redir_read(gsi);
        if (entry == 0 || (entry & 0xFFu) < 32) return;  // unrouted
        ioapic_redir_write(gsi, entry & ~static_cast<hcpp::u64>(redir_masked));
    }

    hcpp::u8 apic_gsi_vector(hcpp::u32 gsi) noexcept {
        return gsi < max_tracked_gsis ? gsi_vector[gsi] : 0;
    }
}  // namespace hype::x86
