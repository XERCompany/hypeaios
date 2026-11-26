#pragma once
#include "kernel/pci/pci.h"
namespace hype::pci {
    constexpr hcpp::u8 capability_power = 0x01, capability_msi = 0x05, capability_msix = 0x11, capability_pcie = 0x10;
    bool has_capability_list(const PciDevice& device) noexcept;
    hcpp::u8 find_capability(const PciDevice& device, hcpp::u8 capability) noexcept;
    hcpp::u8 capability_next(const PciDevice& device, hcpp::u8 offset) noexcept;
    hcpp::u16 msi_control(const PciDevice& device) noexcept;
    bool msix_present(const PciDevice& device) noexcept;
}  // namespace hype::pci
