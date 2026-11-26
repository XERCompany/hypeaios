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
}  // namespace hype::pci
