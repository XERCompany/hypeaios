#include "kernel/pci/pci.h"

#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace hype::pci {
    namespace {
        constexpr hcpp::u16 config_address_port = 0xCF8;
        constexpr hcpp::u16 config_data_port = 0xCFC;
        PciDevice found[max_devices];
        hcpp::size_t count;
        hcpp::size_t bridge_count;
        hcpp::u8 visited_buses[32];  // 256-bit bitmap, one bit per bus
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
        bool bus_seen(hcpp::u8 bus) noexcept {
            return (visited_buses[bus >> 3] & hcpp::u8(1u << (bus & 7))) != 0;
        }
        void mark_bus(hcpp::u8 bus) noexcept {
            visited_buses[bus >> 3] = static_cast<hcpp::u8>(visited_buses[bus >> 3] | hcpp::u8(1u << (bus & 7)));
        }
        bool read_device(hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function, PciDevice &output) noexcept {
            const hcpp::u16 vendor = config_read16(bus, slot, function, 0);
            if (vendor == invalid_vendor) return false;
            const hcpp::u8 header = config_read8(bus, slot, function, 14);
            output.bus = bus;
            output.slot = slot;
            output.function = function;
            output.vendor_id = vendor;
            output.device_id = config_read16(bus, slot, function, 2);
            output.class_code = config_read8(bus, slot, function, 11);
            output.subclass = config_read8(bus, slot, function, 10);
            output.program_interface = config_read8(bus, slot, function, 9);
            output.revision = config_read8(bus, slot, function, 8);
            output.header_type = static_cast<hcpp::u8>(header & 0x7F);
            output.interrupt_line = config_read8(bus, slot, function, 0x3C);
            output.interrupt_pin = config_read8(bus, slot, function, 0x3D);
            output.is_bridge = output.header_type == 0x01 ||
                               (output.class_code == 0x06 && output.subclass == 0x04);
            const hcpp::u8 bar_total =
                output.header_type == 0x00 ? 6 : (output.header_type == 0x01 ? 2 : 1);
            for (hcpp::u8 i = 0; i < 6; ++i)
                output.bars[i] = i < bar_total
                                     ? config_read32(bus, slot, function,
                                                     static_cast<hcpp::u8>(0x10 + i * 4))
                                     : 0;
            return true;
        }
        void scan_bus(hcpp::u8 bus) noexcept {
            if (bus_seen(bus)) return;
            mark_bus(bus);
            for (hcpp::u8 slot = 0; slot < 32; ++slot) {
                if (count >= max_devices) return;
                if (config_read16(bus, slot, 0, 0) == invalid_vendor) continue;
                const hcpp::u8 functions =
                    (config_read8(bus, slot, 0, 14) & 0x80) != 0 ? 8 : 1;
                for (hcpp::u8 fn = 0; fn < functions; ++fn) {
                    if (count >= max_devices) return;
                    PciDevice dev{};
                    if (!read_device(bus, slot, fn, dev)) continue;
                    found[count++] = dev;
                    if (dev.is_bridge) {
                        ++bridge_count;
                        const hcpp::u8 secondary = config_read8(bus, slot, fn, 0x19);
                        if (secondary != 0) scan_bus(secondary);
                    }
                }
            }
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
        bridge_count = 0;
        for (hcpp::u8 &entry : visited_buses) entry = 0;
        scan_bus(0);
        serial::write("pci devs=");
        serial::write_dec32(static_cast<hcpp::u32>(count));
        serial::write(" bridges=");
        serial::write_dec32(static_cast<hcpp::u32>(bridge_count));
        serial::write("\r\n");
        bool bar_size_ok = false;
        if (const PciDevice *display = find_class(0x03); display != nullptr)
            bar_size_ok = pci_bar_size(*display, 0) != 0;
        serial::write("pci bar=size ");
        serial::write_line(bar_size_ok ? "ok" : "fail");
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
        return index < 6 ? device.bars[index] : 0;
    }
}  // namespace hype::pci
