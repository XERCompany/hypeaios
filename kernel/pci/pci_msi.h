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
}  // namespace hype::pci
