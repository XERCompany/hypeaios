#include "kernel/pci/pci_capability.h"
namespace hype::pci {
    bool has_capability_list(const PciDevice& device) noexcept {
        return (config_read16(device.bus, device.slot, device.function, 0x06) & 0x10) != 0;
    }
    hcpp::u8 capability_next(const PciDevice& device, hcpp::u8 offset) noexcept {
        return offset >= 0x40 ? config_read8(device.bus, device.slot, device.function, offset + 1) : 0;
    }
    hcpp::u8 find_capability(const PciDevice& device, hcpp::u8 capability) noexcept {
        if (!has_capability_list(device)) return 0;
        hcpp::u8 pos = config_read8(device.bus, device.slot, device.function, 0x34) & 0xFC;
        for (hcpp::u8 steps = 0; pos >= 0x40 && steps < 32; ++steps) {
            if (config_read8(device.bus, device.slot, device.function, pos) == capability) return pos;
            pos = capability_next(device, pos) & 0xFC;
        }
        return 0;
    }
    hcpp::u16 msi_control(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msi);
        return pos ? config_read16(device.bus, device.slot, device.function, pos + 2) : 0;
    }
    bool msix_present(const PciDevice& device) noexcept { return find_capability(device, capability_msix) != 0; }
    hcpp::u16 pcie_caps_find(const PciDevice& device, hcpp::u16 capability_id) noexcept {
        // Extended capabilities sit in the 0x100..0xFFF range, which is
        // only reachable through ECAM. On a CF8/CFC-only bus the first
        // read returns all-ones and the walk ends immediately.
        hcpp::u16 pos = 0x100;
        for (hcpp::u8 steps = 0; pos >= 0x100 && pos <= 0xFFC && steps < 64; ++steps) {
            const hcpp::u32 header = config_read32(device.bus, device.slot, device.function, pos);
            if (header == 0 || header == 0xFFFFFFFFu) return 0;
            if ((header & 0xFFFFu) == capability_id) return pos;
            pos = static_cast<hcpp::u16>((header >> 20) & 0xFFCu);  // next-pointer, bits [31:20]
            if (pos == 0) return 0;
        }
        return 0;
    }
    hcpp::u16 pcie_ext_cap_count(const PciDevice& device) noexcept {
        hcpp::u16 count = 0;
        hcpp::u16 pos = 0x100;
        for (hcpp::u8 steps = 0; pos >= 0x100 && pos <= 0xFFC && steps < 64; ++steps) {
            const hcpp::u32 header = config_read32(device.bus, device.slot, device.function, pos);
            if (header == 0 || header == 0xFFFFFFFFu) break;
            ++count;
            pos = static_cast<hcpp::u16>((header >> 20) & 0xFFCu);
            if (pos == 0) break;
        }
        return count;
    }
    hcpp::u8 pcie_device_type(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_pcie);
        if (pos == 0) return 0xFF;
        return static_cast<hcpp::u8>((config_read16(device.bus, device.slot, device.function, pos + 2) >> 4) & 0xF);
    }
    bool pcie_is_endpoint(const PciDevice& device) noexcept {
        const hcpp::u8 type = pcie_device_type(device);
        return type == 0x0 || type == 0x1;  // endpoint / legacy endpoint
    }
}  // namespace hype::pci
