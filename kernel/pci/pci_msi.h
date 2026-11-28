#pragma once
#include "kernel/pci/pci_capability.h"
namespace hype::pci {
    struct MsiConfig {
        hcpp::u64 address;
        hcpp::u16 data;
        hcpp::u8 vectors;
        bool enabled;
    };
    bool msi_supported(const PciDevice& device) noexcept;
    bool msi_read(const PciDevice& device, MsiConfig& output) noexcept;
    bool msi_enable(const PciDevice& device, hcpp::u64 address, hcpp::u16 data) noexcept;
    bool msi_disable(const PciDevice& device) noexcept;

    // Programs the MSI capability for one vector aimed at a LAPIC:
    // message address 0xFEE00000 | (lapic_id << 12), message data =
    // vector (fixed delivery, physical destination, edge triggered),
    // then sets the capability enable bit and caches msi_enabled /
    // irq_vector on the canonical PciDevice entry. Returns false when
    // the device has no MSI capability.
    bool pci_msi_setup(const PciDevice& device, hcpp::u8 vector, hcpp::u32 lapic_id) noexcept;
}  // namespace hype::pci
