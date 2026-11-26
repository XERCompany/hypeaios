#pragma once
#include "kernel/pci/pci.h"
namespace hype::pci {
    void config_write16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset, hcpp::u16 value) noexcept;
    hcpp::u32 bar_read(const PciDevice& device, hcpp::u8 index) noexcept;
    void bar_write(const PciDevice& device, hcpp::u8 index, hcpp::u32 value) noexcept;
    bool bar_is_memory(hcpp::u32 value) noexcept;
    bool bar_is_io(hcpp::u32 value) noexcept;
    hcpp::u64 bar_memory_address(hcpp::u32 low, hcpp::u32 high = 0) noexcept;
    hcpp::u16 bar_io_address(hcpp::u32 value) noexcept;
    void io_write32(hcpp::u16 port, hcpp::u32 value) noexcept;
    hcpp::u32 io_read32(hcpp::u16 port) noexcept;
}  // namespace hype::pci
