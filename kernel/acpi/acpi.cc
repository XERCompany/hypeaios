#include "kernel/acpi/acpi.h"

namespace hype::acpi {
    namespace {
        AcpiInfo current;
        bool signature_equals(const char* left, const char* right, hcpp::size_t size) noexcept {
            for (hcpp::size_t i = 0; i < size; ++i)
                if (left[i] != right[i]) return false;
            return true;
        }
        const RsdpDescriptor* scan_range(hcpp::uintptr_t base, hcpp::size_t size) noexcept {
            for (hcpp::uintptr_t address = base; address + 20 <= base + size; address += 16) {
                const auto* rsdp = reinterpret_cast<const RsdpDescriptor*>(address);
                if (signature_equals(rsdp->signature, "RSD PTR ", 8) && valid_checksum(rsdp, 20)) return rsdp;
            }
            return nullptr;
        }
        const SdtHeader* table_at(hcpp::uintptr_t address) noexcept {
            const auto* table = reinterpret_cast<const SdtHeader*>(address);
            if (table->length < sizeof(SdtHeader) || table->length > 1024 * 1024) return nullptr;
            return valid_checksum(table, table->length) ? table : nullptr;
        }
    }  // namespace
    void initialize() noexcept {
        current = {};
        const RsdpDescriptor* rsdp = find_rsdp();
        if (rsdp == nullptr) return;
        current.present = true;
        current.rsdp = reinterpret_cast<hcpp::uintptr_t>(rsdp);
        current.xsdt = rsdp->revision >= 2 && rsdp->xsdt_address != 0;
        current.root_table = current.xsdt ? static_cast<hcpp::uintptr_t>(rsdp->xsdt_address) : rsdp->rsdt_address;
        const SdtHeader* root = table_at(current.root_table);
        if (root == nullptr) {
            current.present = false;
            return;
        }
        const hcpp::size_t entry_size = current.xsdt ? 8 : 4;
        current.table_count = root->length > sizeof(SdtHeader) ? (root->length - sizeof(SdtHeader)) / entry_size : 0;
        for (hcpp::size_t i = 0; i < current.table_count && i < 32; ++i) {
            hcpp::uintptr_t address = 0;
            if (current.xsdt)
                address = static_cast<hcpp::uintptr_t>(reinterpret_cast<const hcpp::u64*>(root + 1)[i]);
            else
                address = reinterpret_cast<const hcpp::u32*>(root + 1)[i];
            const SdtHeader* table = table_at(address);
            if (table == nullptr) continue;
            if (signature_equals(table->signature, "APIC", 4)) current.madt = address;
            if (signature_equals(table->signature, "FACP", 4)) current.fadt = address;
        }
    }
    bool valid_checksum(const void* data, hcpp::size_t size) noexcept {
        const auto* bytes = static_cast<const hcpp::u8*>(data);
        hcpp::u8 sum = 0;
        for (hcpp::size_t i = 0; i < size; ++i) sum = static_cast<hcpp::u8>(sum + bytes[i]);
        return sum == 0;
    }
    const RsdpDescriptor* find_rsdp() noexcept {
        const hcpp::u16 ebda_segment = *reinterpret_cast<const hcpp::u16*>(0x40E);
        if (ebda_segment != 0) {
            const hcpp::uintptr_t ebda = static_cast<hcpp::uintptr_t>(ebda_segment) << 4;
            if (const RsdpDescriptor* rsdp = scan_range(ebda, 1024)) return rsdp;
        }
        return scan_range(0xE0000, 0x20000);
    }
    const SdtHeader* find_table(const char signature[4]) noexcept {
        if (!current.present || current.root_table == 0) return nullptr;
        const auto* root = reinterpret_cast<const SdtHeader*>(current.root_table);
        const hcpp::size_t entry_size = current.xsdt ? 8 : 4;
        for (hcpp::size_t i = 0; i < current.table_count && i < 32; ++i) {
            hcpp::uintptr_t address =
                current.xsdt ? static_cast<hcpp::uintptr_t>(reinterpret_cast<const hcpp::u64*>(root + 1)[i])
                             : reinterpret_cast<const hcpp::u32*>(root + 1)[i];
            const SdtHeader* table = table_at(address);
            if (table != nullptr && signature_equals(table->signature, signature, 4)) return table;
        }
        return nullptr;
    }
    const AcpiInfo& info() noexcept { return current; }
    const char* table_signature(const SdtHeader* table, char output[5]) noexcept {
        for (int i = 0; i < 4; ++i) output[i] = table->signature[i];
        output[4] = '\0';
        return output;
    }
}  // namespace hype::acpi
