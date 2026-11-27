#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::display {

    constexpr hcpp::u16 dispi_ioport_index = 0x1CE;
    constexpr hcpp::u16 dispi_ioport_data = 0x1CF;

    constexpr hcpp::u16 dispi_index_id = 0x0;
    constexpr hcpp::u16 dispi_index_xres = 0x1;
    constexpr hcpp::u16 dispi_index_yres = 0x2;
    constexpr hcpp::u16 dispi_index_bpp = 0x3;
    constexpr hcpp::u16 dispi_index_enable = 0x4;
    constexpr hcpp::u16 dispi_index_bank = 0x5;
    constexpr hcpp::u16 dispi_index_virt_width = 0x6;
    constexpr hcpp::u16 dispi_index_virt_height = 0x7;
    constexpr hcpp::u16 dispi_index_x_offset = 0x8;
    constexpr hcpp::u16 dispi_index_y_offset = 0x9;
    constexpr hcpp::u16 dispi_index_vmem_64k = 0xA;

    constexpr hcpp::u16 dispi_id_min = 0xB0C0;
    constexpr hcpp::u16 dispi_id_max = 0xB0C5;

    constexpr hcpp::u16 dispi_disabled = 0x00;
    constexpr hcpp::u16 dispi_enabled = 0x01;
    constexpr hcpp::u16 dispi_getcaps = 0x02;
    constexpr hcpp::u16 dispi_8bit_dac = 0x20;
    constexpr hcpp::u16 dispi_lfb_enabled = 0x40;
    constexpr hcpp::u16 dispi_noclearmem = 0x80;

    constexpr hcpp::u16 bochs_vendor_id = 0x1234;
    constexpr hcpp::u16 bochs_device_id = 0x1111;
    constexpr hcpp::u32 bochs_mmio_dispi_reg = 0x500;
    constexpr hcpp::u8 bochs_lfb_bar = 0;
    constexpr hcpp::u8 bochs_mmio_bar = 2;

    constexpr hcpp::u32 dispi_max_width = 2560;
    constexpr hcpp::u32 dispi_max_height = 1600;

    struct DispiMode {
        hcpp::u32 width = 0;
        hcpp::u32 height = 0;
        hcpp::u32 bpp = 0;
        bool enabled = false;
        bool lfb = false;
    };

    struct DispiInfo {
        bool present = false;
        bool mmio = false;
        bool port_only = false;
        hcpp::u16 id = 0;
        hcpp::u64 lfb_address = 0;
        bool lfb_from_pci = false;
        hcpp::u32 vmem_64k = 0;
    };

    void dispi_init() noexcept;
    const DispiInfo& dispi_info() noexcept;
    bool dispi_present() noexcept;
    const pci::PciDevice* dispi_device() noexcept;
    hcpp::u16 dispi_read_id() noexcept;
    hcpp::u16 dispi_read(hcpp::u16 index) noexcept;
    void dispi_write(hcpp::u16 index, hcpp::u16 value) noexcept;
    bool dispi_get_mode(DispiMode& mode) noexcept;
    bool dispi_set_mode(hcpp::u32 width, hcpp::u32 height,
                        hcpp::u32 bpp) noexcept;
    hcpp::u64 dispi_lfb_address() noexcept;

}  // namespace hype::display
