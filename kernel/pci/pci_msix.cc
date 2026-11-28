#include "kernel/pci/pci_msix.h"

#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace hype::pci {
    namespace {
        constexpr hcpp::u16 msix_ctrl_size_mask = 0x03FF;
        constexpr hcpp::u16 msix_ctrl_function_mask = 1u << 14;
        constexpr hcpp::u16 msix_ctrl_enable = 1u << 15;
        constexpr hcpp::u32 msix_entry_mask = 1u;

        // One 16-byte MSI-X table entry: {addr lo, addr hi, data, ctrl}.
        struct MsixEntry {
            volatile hcpp::u32 address_low;
            volatile hcpp::u32 address_high;
            volatile hcpp::u32 data;
            volatile hcpp::u32 control;
        };

        volatile MsixEntry* entry_at(volatile hcpp::u32* table, hcpp::u16 index) noexcept {
            return reinterpret_cast<volatile MsixEntry*>(table) + index;
        }
    }  // namespace

    hcpp::u16 msix_table_size(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msix);
        if (pos == 0) return 0;
        return static_cast<hcpp::u16>(
            (config_read16(device.bus, device.slot, device.function, pos + 2) & msix_ctrl_size_mask) + 1);
    }

    bool msix_read(const PciDevice& device, MsixConfig& output) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msix);
        if (pos == 0) return false;
        const hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        const hcpp::u32 table = config_read32(device.bus, device.slot, device.function, pos + 4);
        const hcpp::u32 pba = config_read32(device.bus, device.slot, device.function, pos + 8);
        output = {static_cast<hcpp::u16>((control & msix_ctrl_size_mask) + 1),
                  static_cast<hcpp::u8>(table & 7),
                  table & ~7u,
                  static_cast<hcpp::u8>(pba & 7),
                  pba & ~7u,
                  (control & msix_ctrl_enable) != 0,
                  (control & msix_ctrl_function_mask) != 0};
        return true;
    }

    volatile hcpp::u32* msix_table(const PciDevice& device) noexcept {
        MsixConfig config{};
        if (!msix_read(device, config) || config.table_bir >= 6) return nullptr;
        const hcpp::u32 low = device.bars[config.table_bir];
        if (!bar_is_memory(low)) return nullptr;
        const bool wide = (low & 0x6) == 0x4;
        const hcpp::u32 high = wide && config.table_bir + 1 < 6 ? device.bars[config.table_bir + 1] : 0;
        const hcpp::u64 address = bar_memory_address(low, high) + config.table_offset;
        // The boot page tables identity-map only the low 4 GiB.
        if (address >= 0x100000000ull) return nullptr;
        return reinterpret_cast<volatile hcpp::u32*>(address);
    }

    bool msix_program_vector(const PciDevice& device, hcpp::u16 index, hcpp::u64 address,
                             hcpp::u32 data) noexcept {
        volatile hcpp::u32* table = msix_table(device);
        if (table == nullptr || index >= msix_table_size(device)) return false;
        volatile MsixEntry* entry = entry_at(table, index);
        entry->control = msix_entry_mask;  // masked while programming
        entry->address_low = static_cast<hcpp::u32>(address);
        entry->address_high = static_cast<hcpp::u32>(address >> 32);
        entry->data = data;
        return true;
    }

    bool msix_set_mask(const PciDevice& device, hcpp::u16 index, bool masked) noexcept {
        volatile hcpp::u32* table = msix_table(device);
        if (table == nullptr || index >= msix_table_size(device)) return false;
        volatile MsixEntry* entry = entry_at(table, index);
        entry->control = masked ? (entry->control | msix_entry_mask) : (entry->control & ~msix_entry_mask);
        return true;
    }

    bool msix_enable(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msix);
        if (pos == 0) return false;
        const hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        config_write16(device.bus, device.slot, device.function, pos + 2,
                       static_cast<hcpp::u16>((control & ~msix_ctrl_function_mask) | msix_ctrl_enable));
        return true;
    }

    bool msix_disable(const PciDevice& device) noexcept {
        const hcpp::u8 pos = find_capability(device, capability_msix);
        if (pos == 0) return false;
        const hcpp::u16 control = config_read16(device.bus, device.slot, device.function, pos + 2);
        config_write16(device.bus, device.slot, device.function, pos + 2,
                       static_cast<hcpp::u16>(control & ~msix_ctrl_enable));
        if (PciDevice* entry = device_lookup(device.bus, device.slot, device.function); entry != nullptr)
            entry->msix_enabled = false;
        return true;
    }

    bool pci_msix_setup(const PciDevice& device, hcpp::u8 vector, hcpp::u32 lapic_id) noexcept {
        if (!msix_present(device)) return false;
        const hcpp::u64 address = 0xFEE00000ull | (hcpp::u64(lapic_id & 0xFF) << 12);
        if (!msix_program_vector(device, 0, address, vector)) return false;
        if (!msix_enable(device)) return false;
        if (!msix_set_mask(device, 0, false)) return false;
        if (PciDevice* entry = device_lookup(device.bus, device.slot, device.function); entry != nullptr) {
            entry->msix_enabled = true;
            entry->irq_vector = vector;
        }
        serial::write("msix setup=ok vec=");
        serial::write_dec32(vector);
        serial::write("\r\n");
        return true;
    }
}  // namespace hype::pci
