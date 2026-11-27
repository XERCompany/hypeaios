#pragma once

#include "hcpp/types.hpp"

namespace hype::pci {

    struct PciDevice {
        hcpp::u8 bus;
        hcpp::u8 slot;
        hcpp::u8 function;
        hcpp::u16 vendor_id;
        hcpp::u16 device_id;
        hcpp::u8 class_code;
        hcpp::u8 subclass;
        hcpp::u8 program_interface;
        hcpp::u8 revision;
        hcpp::u8 header_type;
        hcpp::u8 interrupt_line;
        hcpp::u8 interrupt_pin;
        bool is_bridge;
        hcpp::u32 bars[6];
    };

    constexpr hcpp::size_t max_devices = 256;
    constexpr hcpp::u16 invalid_vendor = 0xFFFF;

    hcpp::u32 config_read32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept;
    hcpp::u16 config_read16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept;
    hcpp::u8 config_read8(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept;
    void config_write32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset, hcpp::u32 value) noexcept;
    hcpp::size_t scan() noexcept;
    const PciDevice *devices() noexcept;
    hcpp::size_t device_count() noexcept;
    const PciDevice *find(hcpp::u16 vendor_id, hcpp::u16 device_id) noexcept;
    const PciDevice *find_class(hcpp::u8 class_code, hcpp::u8 subclass = 0xFF) noexcept;
    hcpp::u32 bar(const PciDevice &device, hcpp::u8 index) noexcept;

}  // namespace hype::pci
