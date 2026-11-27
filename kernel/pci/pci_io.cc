#include "kernel/pci/pci_io.h"
namespace hype::pci {
    void config_write16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset, hcpp::u16 value) noexcept {
        const hcpp::u8 aligned = offset & ~3;
        const hcpp::u8 shift = (offset & 2) * 8;
        hcpp::u32 value32 = config_read32(bus, slot, function, aligned);
        value32 = (value32 & ~(hcpp::u32(0xFFFF) << shift)) | (hcpp::u32(value) << shift);
        config_write32(bus, slot, function, aligned, value32);
    }
    hcpp::u32 bar_read(const PciDevice& device, hcpp::u8 index) noexcept { return bar(device, index); }
    void bar_write(const PciDevice& device, hcpp::u8 index, hcpp::u32 value) noexcept {
        if (index < 6) config_write32(device.bus, device.slot, device.function, 0x10 + index * 4, value);
    }
    bool bar_is_memory(hcpp::u32 value) noexcept { return value != 0 && (value & 1) == 0; }
    bool bar_is_io(hcpp::u32 value) noexcept { return (value & 1) != 0; }
    hcpp::u64 bar_memory_address(hcpp::u32 low, hcpp::u32 high) noexcept {
        if (!bar_is_memory(low)) return 0;
        const bool wide = (low & 0x6) == 0x4;
        return (hcpp::u64(wide ? high : 0) << 32) | (low & 0xFFFFFFF0u);
    }
    hcpp::u16 bar_io_address(hcpp::u32 value) noexcept {
        return bar_is_io(value) ? static_cast<hcpp::u16>(value & 0xFFFC) : 0;
    }
    void io_write32(hcpp::u16 port, hcpp::u32 value) noexcept { asm volatile("outl %0,%1" ::"a"(value), "Nd"(port)); }
    hcpp::u32 io_read32(hcpp::u16 port) noexcept {
        hcpp::u32 v;
        asm volatile("inl %1,%0" : "=a"(v) : "Nd"(port));
        return v;
    }
    hcpp::u32 pci_bar_size(const PciDevice& device, hcpp::u8 index) noexcept {
        if (index >= 6) return 0;
        const hcpp::u8 offset = static_cast<hcpp::u8>(0x10 + index * 4);
        const hcpp::u32 original = config_read32(device.bus, device.slot, device.function, offset);
        if (original == 0) return 0;
        config_write32(device.bus, device.slot, device.function, offset, 0xFFFFFFFFu);
        const hcpp::u32 probed = config_read32(device.bus, device.slot, device.function, offset);
        config_write32(device.bus, device.slot, device.function, offset, original);
        if (probed == 0 || probed == 0xFFFFFFFFu) return 0;
        const hcpp::u32 mask = bar_is_io(original) ? (probed & ~0x3u) : (probed & ~0xFu);
        return mask == 0 ? 0 : (~mask + 1u);
    }
    void pci_set_command(const PciDevice& device, hcpp::u16 bits) noexcept {
        const hcpp::u16 command = config_read16(device.bus, device.slot, device.function, 0x04);
        config_write16(device.bus, device.slot, device.function, 0x04,
                       static_cast<hcpp::u16>(command | bits));
    }
    void pci_enable_busmaster(const PciDevice& device) noexcept { pci_set_command(device, command_bus_master); }
    void pci_enable_mem(const PciDevice& device) noexcept { pci_set_command(device, command_memory); }
    void pci_enable_io(const PciDevice& device) noexcept { pci_set_command(device, command_io); }
}  // namespace hype::pci
