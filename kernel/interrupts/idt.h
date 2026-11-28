#pragma once
#include "hcpp/types.hpp"
#include "kernel/irq.h"
namespace hype::interrupts {
    struct IdtEntry {
        hcpp::u16 offset_low;
        hcpp::u16 selector;
        hcpp::u8 ist;
        hcpp::u8 type_attributes;
        hcpp::u16 offset_middle;
        hcpp::u32 offset_high;
        hcpp::u32 zero;
    } __attribute__((packed));
    static_assert(sizeof(IdtEntry) == 16);
    void idt_initialize() noexcept;
    void pic_initialize() noexcept;
    void pic_mask_all() noexcept;
    void pic_unmask(hcpp::u8 irq) noexcept;
    void pic_send_eoi(hcpp::u8 irq) noexcept;
    // Unmasks a legacy IRQ on whichever controller is live: the 8259
    // pair, or the IOAPIC once x86::apic_initialize() has switched over.
    void irq_enable(hcpp::u8 irq) noexcept;
    // Secondary dispatch table for vectors 48..255 (MSI/MSI-X and
    // other non-legacy interrupt sources; the irq_dispatch table only
    // covers the 16 legacy lines on vectors 32..47). The handler gets
    // the vector number as its irq argument; a LAPIC EOI is sent after
    // the handler returns. Registration fails for vectors < 48.
    bool idt_vector_register(hcpp::u8 vector, kernel::IrqHandler handler, void* context) noexcept;
    void idt_vector_unregister(hcpp::u8 vector) noexcept;
    void pit_initialize(hcpp::u32 frequency) noexcept;
    void keyboard_initialize() noexcept;
    void mouse_initialize() noexcept;
    hcpp::u64 pit_ticks() noexcept;
    void pit_wait_ticks(hcpp::u64 ticks) noexcept;
    bool interrupts_initialized() noexcept;
    void interrupts_enable() noexcept;
    void interrupts_disable() noexcept;
}  // namespace hype::interrupts
