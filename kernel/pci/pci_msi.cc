#include "kernel/pci/pci_msi.h"

#include "kernel/pci/pci_io.h"
namespace hype::pci {
    bool msi_supported(const PciDevice& device) noexcept { return find_capability(device, capability_msi) != 0; }
    bool msi_read(const PciDevice& device, MsiConfig& output) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msi);
        if (!pos) return false;
        const hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        const bool wide = (control & 0x80) != 0;
        const hcpp::u32 low = config_read32(device.bus, device.slot, device.function, pos + 4);
        const hcpp::u32 high = wide ? config_read32(device.bus, device.slot, device.function, pos + 8) : 0;
        const hcpp::u8 data_offset = wide ? 0x0C : 0x08;
        output = {(hcpp::u64(high) << 32) | low,
                  config_read16(device.bus, device.slot, device.function, pos + data_offset),
                  static_cast<hcpp::u8>(1 << ((control >> 1) & 7)), (control & 1) != 0};
        return true;
    }
    bool msi_enable(const PciDevice& device, hcpp::u64 address, hcpp::u16 data) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msi);
        if (!pos) return false;
        hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        const bool wide = (control & 0x80) != 0;
        config_write32(device.bus, device.slot, device.function, pos + 4, hcpp::u32(address));
        if (wide) config_write32(device.bus, device.slot, device.function, pos + 8, hcpp::u32(address >> 32));
        config_write16(device.bus, device.slot, device.function, pos + (wide ? 0x0C : 0x08), data);
        config_write16(device.bus, device.slot, device.function, pos + 2, control | 1);
        return true;
    }
    bool msi_disable(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msi);
        if (!pos) return false;
        const hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        config_write16(device.bus, device.slot, device.function, pos + 2, control & ~hcpp::u16(1));
        return true;
    }
}  // namespace hype::pci
