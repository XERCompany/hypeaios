#include "kernel/audio/audiow2.h"

#include "kernel/audio/audiomixart.h"
#include "kernel/audio/audionm256.h"
#include "kernel/audio/audiooxygen.h"
#include "kernel/audio/audiopcxhr.h"
#include "kernel/audio/audioriptide.h"
#include "kernel/audio/audiorme9652.h"
#include "kernel/audio/audiotrident.h"
#include "kernel/audio/audiovx222.h"
#include "kernel/audio/audioymfpci.h"
#include "kernel/serial.hpp"
#include "kernel/vfs/hypefs.h"

namespace hype::audio::w2 {
    namespace {

        hcpp::size_t str_len(const char *text) noexcept {
            hcpp::size_t len = 0;
            while (text[len] != 0) {
                ++len;
            }
            return len;
        }

        struct W2Driver {
            const char *name;
            bool (*probe)() noexcept;
        };

        constexpr W2Driver wave2_drivers[] = {
            {"mixart", &mixart::mixart_selftest},
            {"nm256", &nm256::nm256_selftest},
            {"oxygen", &oxygen::oxygen_selftest},
            {"pcxhr", &pcxhr::pcxhr_selftest},
            {"riptide", &riptide::riptide_selftest},
            {"rme9652", &rme9652::rme9652_selftest},
            {"trident", &trident::trident_selftest},
            {"vx222", &vx222::vx222_selftest},
            {"ymfpci", &ymfpci::ymfpci_selftest},
        };

    }  // namespace

    void settle(hcpp::u32 spins) noexcept {
        for (hcpp::u32 i = 0; i < spins; ++i) {
            asm volatile("pause");
        }
    }

    void hex16(hcpp::u16 value) noexcept {
        for (int shift = 12; shift >= 0; shift -= 4) {
            const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
            serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                          : static_cast<char>('a' + digit - 10));
        }
    }

    void hex32(hcpp::u32 value) noexcept {
        for (int shift = 28; shift >= 0; shift -= 4) {
            const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
            serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                          : static_cast<char>('a' + digit - 10));
        }
    }

    const pci::PciDevice *find_ids(hcpp::u16 vendor, const hcpp::u16 *ids,
                                   hcpp::size_t count) noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t total = pci::device_count();
        for (hcpp::size_t i = 0; i < total; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id != vendor) {
                continue;
            }
            for (hcpp::size_t j = 0; j < count; ++j) {
                if (device.device_id == ids[j]) {
                    return &device;
                }
            }
        }
        return nullptr;
    }

    const pci::PciDevice *find_sub(hcpp::u16 vendor, const hcpp::u16 *ids,
                                   hcpp::size_t count,
                                   hcpp::u16 subvendor) noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t total = pci::device_count();
        for (hcpp::size_t i = 0; i < total; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id != vendor) {
                continue;
            }
            bool id_match = false;
            for (hcpp::size_t j = 0; j < count; ++j) {
                if (device.device_id == ids[j]) {
                    id_match = true;
                    break;
                }
            }
            if (!id_match) {
                continue;
            }
            if (subsystem_vendor(device) != subvendor) {
                continue;
            }
            return &device;
        }
        return nullptr;
    }

    hcpp::u16 subsystem_vendor(const pci::PciDevice &device) noexcept {
        return pci::config_read16(device.bus, device.slot, device.function,
                                  pci_subsystem_id_reg);
    }

    bool enable(const pci::PciDevice &device, bool io, bool mem) noexcept {
        const hcpp::u32 command = pci::config_read32(
            device.bus, device.slot, device.function, pci_command_reg);
        hcpp::u32 bits = pci_cmd_bus_master;
        if (io) {
            bits |= pci_cmd_io_space;
        }
        if (mem) {
            bits |= pci_cmd_memory_space;
        }
        pci::config_write32(device.bus, device.slot, device.function,
                            pci_command_reg, (command & 0xFFFF) | bits);
        const hcpp::u32 read_back = pci::config_read32(
            device.bus, device.slot, device.function, pci_command_reg);
        return (read_back & bits) == bits;
    }

    hcpp::u16 io_base(const pci::PciDevice &device, hcpp::u8 bar) noexcept {
        if (bar >= 6) {
            return 0;
        }
        const hcpp::u32 raw = pci::bar(device, bar);
        if ((raw & 0x1) == 0) {
            return 0;
        }
        const hcpp::u32 port = raw & ~hcpp::u32(0x3);
        if (port == 0 || port > 0xFF00) {
            return 0;
        }
        return static_cast<hcpp::u16>(port);
    }

    hcpp::uintptr_t mem_base(const pci::PciDevice &device, hcpp::u8 bar) noexcept {
        if (bar >= 6) {
            return 0;
        }
        const hcpp::u32 low = pci::bar(device, bar);
        if ((low & 0x1) != 0 || low == 0) {
            return 0;
        }
        hcpp::uintptr_t base = low & ~hcpp::u32(0xF);
        if ((low & 0x6) == 0x4 && bar + 1 < 6) {
            const hcpp::u32 high = pci::bar(device, bar + 1);
            base |= static_cast<hcpp::uintptr_t>(high) << 32;
        }
        if (base == 0 || base >= 0x100000000ull) {
            return 0;
        }
        return base;
    }

    bool firmware_load(const char *name, void *buffer, hcpp::u32 capacity,
                       hcpp::u32 *out_size) noexcept {
        char path[64];
        const char prefix[] = "HYPEOS://U/firmware/";
        const char suffix[] = ".bin";
        hcpp::size_t pos = 0;
        const hcpp::size_t pre = str_len(prefix);
        for (hcpp::size_t i = 0; i < pre && pos < sizeof(path) - 1; ++i) {
            path[pos++] = prefix[i];
        }
        for (hcpp::size_t i = 0; i < str_len(name) && pos < sizeof(path) - 1; ++i) {
            path[pos++] = name[i];
        }
        const hcpp::size_t suf = str_len(suffix);
        for (hcpp::size_t i = 0; i < suf && pos < sizeof(path) - 1; ++i) {
            path[pos++] = suffix[i];
        }
        path[pos] = 0;
        return vfs::fs_read(path, buffer, capacity, out_size) == vfs::FsStatus::Ok;
    }

    void *firmware_scratch() noexcept {
        alignas(4096) static hcpp::u8 scratch[firmware_scratch_bytes];
        return scratch;
    }

    bool audiow2_probe_all() noexcept {
        hcpp::u32 present = 0;
        for (const W2Driver &driver : wave2_drivers) {
            if (driver.probe()) {
                ++present;
            }
        }
        serial::write("audiow2 drivers=");
        serial::write_dec32(static_cast<hcpp::u32>(
            sizeof(wave2_drivers) / sizeof(wave2_drivers[0])));
        serial::write(" present=");
        serial::write_dec32(present);
        serial::write("\r\n");
        return true;
    }

}  // namespace hype::audio::w2
