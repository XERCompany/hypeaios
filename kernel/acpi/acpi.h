#pragma once

#include "hcpp/types.hpp"

namespace hype::acpi {

    struct RsdpDescriptor {
        char signature[8];
        hcpp::u8 checksum;
        char oem_id[6];
        hcpp::u8 revision;
        hcpp::u32 rsdt_address;
        hcpp::u32 length;
        hcpp::u64 xsdt_address;
        hcpp::u8 extended_checksum;
        hcpp::u8 reserved[3];
    } __attribute__((packed));

    struct SdtHeader {
        char signature[4];
        hcpp::u32 length;
        hcpp::u8 revision;
        hcpp::u8 checksum;
        char oem_id[6];
        char oem_table_id[8];
        hcpp::u32 oem_revision;
        hcpp::u32 creator_id;
        hcpp::u32 creator_revision;
    } __attribute__((packed));

    struct AcpiInfo {
        bool present;
        bool xsdt;
        hcpp::uintptr_t rsdp;
        hcpp::uintptr_t root_table;
        hcpp::size_t table_count;
        hcpp::uintptr_t madt;
        hcpp::uintptr_t fadt;
    };

    void initialize() noexcept;
    bool valid_checksum(const void* data, hcpp::size_t size) noexcept;
    const RsdpDescriptor* find_rsdp() noexcept;
    const SdtHeader* find_table(const char signature[4]) noexcept;
    const AcpiInfo& info() noexcept;
    const char* table_signature(const SdtHeader* table, char output[5]) noexcept;

}  // namespace hype::acpi
