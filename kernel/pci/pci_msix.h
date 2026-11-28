#pragma once
#include "kernel/pci/pci_capability.h"
namespace hype::pci {

    // MSI-X capability layout: Message Control at cap+2 (table size-1
    // in bits [10:0], function mask bit 14, enable bit 15), Table
    // Offset/BIR at cap+4, PBA Offset/BIR at cap+8. Each 16-byte table
    // entry is {msg addr lo, msg addr hi, msg data, vector control}
    // with the per-vector mask in vector-control bit 0.
    struct MsixConfig {
        hcpp::u16 table_size;    // number of entries (1..2048)
        hcpp::u8 table_bir;      // BAR holding the vector table
        hcpp::u32 table_offset;  // byte offset of the table inside the BAR
        hcpp::u8 pba_bir;        // BAR holding the pending-bit array
        hcpp::u32 pba_offset;    // byte offset of the PBA inside the BAR
        bool enabled;
        bool function_masked;
    };

    hcpp::u16 msix_table_size(const PciDevice& device) noexcept;
    bool msix_read(const PciDevice& device, MsixConfig& output) noexcept;

    // Resolves the MSI-X vector table to a CPU address (BAR base +
    // table offset). Returns nullptr when the BAR is not a memory BAR
    // or the table sits at/above 4 GiB (outside the identity map).
    volatile hcpp::u32* msix_table(const PciDevice& device) noexcept;

    bool msix_program_vector(const PciDevice& device, hcpp::u16 index, hcpp::u64 address,
                             hcpp::u32 data) noexcept;
    bool msix_set_mask(const PciDevice& device, hcpp::u16 index, bool masked) noexcept;
    bool msix_enable(const PciDevice& device) noexcept;
    bool msix_disable(const PciDevice& device) noexcept;

    // Programs table entry 0 to deliver `vector` at the given LAPIC,
    // unmasks it, sets the capability enable bit, and caches
    // msix_enabled / irq_vector on the canonical PciDevice entry.
    bool pci_msix_setup(const PciDevice& device, hcpp::u8 vector, hcpp::u32 lapic_id) noexcept;

}  // namespace hype::pci
