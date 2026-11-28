#include "kernel/pci/pci_irq.h"

#include "kernel/arch/x86/apic.h"
#include "kernel/interrupts/idt.h"
#include "kernel/pci/pci_io.h"
#include "kernel/pci/pci_msi.h"
#include "kernel/pci/pci_msix.h"
#include "kernel/serial.hpp"

namespace hype::pci {
    namespace {
        hcpp::u8 next_vector = msi_vector_first;
    }  // namespace

    hcpp::u8 pci_irq_alloc_vector() noexcept {
        return next_vector < msi_vector_limit ? next_vector++ : 0;
    }

    bool pci_irq_install(const PciDevice& device, kernel::IrqHandler handler, void* context) noexcept {
        if (handler == nullptr) return false;
        // MSI bypasses PIC/IOAPIC routing entirely: the device writes
        // the interrupt message straight to the LAPIC at 0xFEE0_xxxx.
        // It therefore only works once the APIC path is live.
        if (x86::apic_active()) {
            const hcpp::u32 lapic_id = x86::apic_lapic_id();
            if (msix_present(device)) {
                const hcpp::u8 vector = pci_irq_alloc_vector();
                if (vector != 0 && pci_msix_setup(device, vector, lapic_id) &&
                    interrupts::idt_vector_register(vector, handler, context)) {
                    pci_set_command(device, command_interrupt_disable);
                    return true;
                }
            }
            if (msi_supported(device)) {
                const hcpp::u8 vector = pci_irq_alloc_vector();
                if (vector != 0 && pci_msi_setup(device, vector, lapic_id) &&
                    interrupts::idt_vector_register(vector, handler, context)) {
                    pci_set_command(device, command_interrupt_disable);
                    return true;
                }
            }
        }
        // Legacy INTx fallback: QEMU's PIIX3 steering already maps
        // INTA-D onto IRQ lines, so interrupt_line is usable directly.
        const hcpp::u8 line = device.interrupt_line;
        if (line < 16 && kernel::irq_register(line, handler, context)) {
            interrupts::irq_enable(line);
            serial::write("pci irq=legacy line=");
            serial::write_dec32(line);
            serial::write("\r\n");
            return true;
        }
        return false;
    }
}  // namespace hype::pci
