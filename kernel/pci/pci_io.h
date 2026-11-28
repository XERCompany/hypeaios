#pragma once
#include "kernel/pci/pci.h"
namespace hype::pci {
    void config_write16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset, hcpp::u16 value) noexcept;
    void config_write8(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset, hcpp::u8 value) noexcept;
    hcpp::u32 bar_read(const PciDevice& device, hcpp::u8 index) noexcept;
    void bar_write(const PciDevice& device, hcpp::u8 index, hcpp::u32 value) noexcept;
    bool bar_is_memory(hcpp::u32 value) noexcept;
    bool bar_is_io(hcpp::u32 value) noexcept;
    hcpp::u64 bar_memory_address(hcpp::u32 low, hcpp::u32 high = 0) noexcept;
    hcpp::u16 bar_io_address(hcpp::u32 value) noexcept;
    void io_write32(hcpp::u16 port, hcpp::u32 value) noexcept;
    hcpp::u32 io_read32(hcpp::u16 port) noexcept;
    constexpr hcpp::u16 command_io = 0x0001;
    constexpr hcpp::u16 command_memory = 0x0002;
    constexpr hcpp::u16 command_bus_master = 0x0004;
    constexpr hcpp::u16 command_interrupt_disable = 0x0400;  // INTx emulation off (MSI/MSI-X active)
    hcpp::u32 pci_bar_size(const PciDevice& device, hcpp::u8 index) noexcept;
    void pci_set_command(const PciDevice& device, hcpp::u16 bits) noexcept;
    void pci_clear_command(const PciDevice& device, hcpp::u16 bits) noexcept;
    void pci_enable_busmaster(const PciDevice& device) noexcept;
    void pci_enable_mem(const PciDevice& device) noexcept;
    void pci_enable_io(const PciDevice& device) noexcept;
}  // namespace hype::pci
