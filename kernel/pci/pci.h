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
        // MSI/MSI-X runtime state, cached on the canonical devices()
        // entry by pci_msi_setup() / pci_msix_setup(). New fields must
        // stay at the tail: audioac97.cc aggregate-initializes the
        // leading members only.
        bool msi_enabled;
        bool msix_enabled;
        hcpp::u8 irq_vector;
    };

    constexpr hcpp::size_t max_devices = 256;
    constexpr hcpp::u16 invalid_vendor = 0xFFFF;

    // Config-space accessors. `offset` is the full 12-bit PCIe register
    // offset: the legacy CF8/CFC path only reaches the first 256 bytes,
    // so offsets >= 0x100 require ECAM (reads return all-ones and
    // writes are dropped when the bus is not covered by an MCFG
    // segment). Per-device the accessor is picked automatically: ECAM
    // for buses inside an MCFG window, CF8/CFC otherwise.
    hcpp::u32 config_read32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset) noexcept;
    hcpp::u16 config_read16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset) noexcept;
    hcpp::u8 config_read8(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset) noexcept;
    void config_write32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u16 offset, hcpp::u32 value) noexcept;

    // True when the MCFG table supplied a usable (< 4 GiB, identity
    // mapped) ECAM base for at least one segment group.
    bool ecam_available() noexcept;

    // ECAM address of a device's 4 KiB config page, or 0 when its bus
    // is not covered by an MCFG segment.
    hcpp::uintptr_t pci_ecam_addr(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function) noexcept;

    hcpp::size_t scan() noexcept;
    const PciDevice *devices() noexcept;
    hcpp::size_t device_count() noexcept;
    const PciDevice *find(hcpp::u16 vendor_id, hcpp::u16 device_id) noexcept;
    const PciDevice *find_class(hcpp::u8 class_code, hcpp::u8 subclass = 0xFF) noexcept;
    hcpp::u32 bar(const PciDevice &device, hcpp::u8 index) noexcept;

    // Mutable view of the canonical scanned-device table entry, used by
    // the MSI/MSI-X setup paths to cache state in PciDevice.
    PciDevice *device_lookup(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function) noexcept;

}  // namespace hype::pci
