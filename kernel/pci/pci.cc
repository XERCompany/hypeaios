#include "kernel/pci/pci.h"

namespace hype::pci {
    namespace {
        constexpr hcpp::u16 config_address_port = 0xCF8;
        constexpr hcpp::u16 config_data_port = 0xCFC;
        PciDevice found[max_devices];
        hcpp::size_t count;
        void out32(hcpp::u16 port, hcpp::u32 value) noexcept { asm volatile("outl %0, %1" ::"a"(value), "Nd"(port)); }
        hcpp::u32 in32(hcpp::u16 port) noexcept {
            hcpp::u32 value;
            asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
            return value;
        }
        hcpp::u32 address(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept {
            return 0x80000000u | (hcpp::u32(bus) << 16) | (hcpp::u32(slot & 31) << 11) |
                   (hcpp::u32(function & 7) << 8) | (offset & 0xFC);
        }
        bool read_device(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, PciDevice &output) noexcept {
            const hcpp::u16 vendor = config_read16(bus, slot, function, 0);
            if (vendor == invalid_vendor) return false;
            output = {bus,
                      slot,
                      function,
                      vendor,
                      config_read16(bus, slot, function, 2),
                      config_read8(bus, slot, function, 11),
                      config_read8(bus, slot, function, 10),
                      config_read8(bus, slot, function, 9),
                      config_read8(bus, slot, function, 8),
                      static_cast<hcpp::u8>(config_read8(bus, slot, function, 14) & 0x7F)};
            return true;
        }
    }  // namespace
    hcpp::u32 config_read32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept {
        out32(config_address_port, address(bus, slot, function, offset));
        return in32(config_data_port);
    }
    hcpp::u16 config_read16(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept {
        return static_cast<hcpp::u16>(config_read32(bus, slot, function, offset) >> ((offset & 2) * 8));
    }
    hcpp::u8 config_read8(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset) noexcept {
        return static_cast<hcpp::u8>(config_read32(bus, slot, function, offset) >> ((offset & 3) * 8));
    }
    void config_write32(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, hcpp::u8 offset, hcpp::u32 value) noexcept {
        out32(config_address_port, address(bus, slot, function, offset));
        out32(config_data_port, value);
    }
    hcpp::size_t scan() noexcept {
        count = 0;
        for (hcpp::u16 bus = 0; bus < 256 && count < max_devices; ++bus)
            for (hcpp::u8 slot = 0; slot < 32 && count < max_devices; ++slot)
                for (hcpp::u8 fn = 0; fn < 8 && count < max_devices; ++fn) {
                    PciDevice dev{};
                    if (read_device(hcpp::u8(bus), slot, fn, dev)) found[count++] = dev;
                }
        return count;
    }
    const PciDevice *devices() noexcept { return found; }
    hcpp::size_t device_count() noexcept { return count; }
    const PciDevice *find(hcpp::u16 vendor, hcpp::u16 device) noexcept {
        for (hcpp::size_t i = 0; i < count; ++i)
            if (found[i].vendor_id == vendor && found[i].device_id == device) return &found[i];
        return nullptr;
    }
    const PciDevice *find_class(hcpp::u8 class_code, hcpp::u8 subclass) noexcept {
        for (hcpp::size_t i = 0; i < count; ++i)
            if (found[i].class_code == class_code && (subclass == 0xFF || found[i].subclass == subclass))
                return &found[i];
        return nullptr;
    }
    hcpp::u32 bar(const PciDevice &device, hcpp::u8 index) noexcept {
        return index < 6 ? config_read32(device.bus, device.slot, device.function, 0x10 + index * 4) : 0;
    }
}  // namespace hype::pci
