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
}  // namespace hype::pci
