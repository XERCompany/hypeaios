#pragma once
#include "kernel/irq.h"
#include "kernel/pci/pci.h"
namespace hype::pci {

    // First IDT vector available for MSI/MSI-X delivery. Vectors
    // 32..47 are the legacy IRQ lines; 0xF0..0xFF stay reserved for
    // APIC self-interrupts / spurious.
    constexpr hcpp::u8 msi_vector_first = 48;
    constexpr hcpp::u8 msi_vector_limit = 0xF0;  // exclusive

    // Allocates the next free vector from the 48..0xEF pool, or 0 when
    // exhausted.
    hcpp::u8 pci_irq_alloc_vector() noexcept;

    // Installs `handler` for `device`, preferring MSI-X, then MSI,
    // then the legacy INTx pin. On the MSI-X/MSI paths a vector is
    // allocated, the interrupt message is aimed at the local APIC, the
    // vector is registered in the IDT secondary dispatch table, and
    // legacy INTx emulation is disabled in the Command register. The
    // legacy path registers `handler` under interrupt_line through
    // irq_register and unmasks the line. On all paths the handler
    // receives the delivering vector/irq number as its second argument.
    // Returns false when no mechanism could be armed.
    bool pci_irq_install(const PciDevice& device, kernel::IrqHandler handler, void* context) noexcept;

}  // namespace hype::pci
