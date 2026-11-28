#pragma once
#include "kernel/pci/pci.h"
namespace hype::pci {
    constexpr hcpp::u8 capability_power = 0x01, capability_msi = 0x05, capability_msix = 0x11, capability_pcie = 0x10;
    // PCIe extended capability IDs live in config space 0x100..0xFFF.
    constexpr hcpp::u16 pcie_ext_cap_aer = 0x0001;
    constexpr hcpp::u16 pcie_ext_cap_vc = 0x0002;
    constexpr hcpp::u16 pcie_ext_cap_serial = 0x0003;
    constexpr hcpp::u16 pcie_ext_cap_acs = 0x000D;
    bool has_capability_list(const PciDevice& device) noexcept;
    hcpp::u8 find_capability(const PciDevice& device, hcpp::u8 capability) noexcept;
    hcpp::u8 capability_next(const PciDevice& device, hcpp::u8 offset) noexcept;
    hcpp::u16 msi_control(const PciDevice& device) noexcept;
    bool msix_present(const PciDevice& device) noexcept;

    // Walks the PCIe extended capability list starting at config offset
    // 0x100 (requires ECAM; returns 0 without it). Returns the absolute
    // config offset of `capability_id`, or 0 when absent.
    hcpp::u16 pcie_caps_find(const PciDevice& device, hcpp::u16 capability_id) noexcept;
    // Number of extended capabilities in the 0x100+ list (0 on
    // CF8-only devices).
    hcpp::u16 pcie_ext_cap_count(const PciDevice& device) noexcept;
    // Device/port type from the PCIe capability register (bits [7:4]
    // of cap+2), or 0xFF when the device has no PCIe capability.
    hcpp::u8 pcie_device_type(const PciDevice& device) noexcept;
    // PCIe endpoint (type 0) or legacy PCIe endpoint (type 1).
    bool pcie_is_endpoint(const PciDevice& device) noexcept;
}  // namespace hype::pci
