#include "kernel/acpi/mcfg.h"

#include "kernel/acpi/acpi.h"
#include "kernel/serial.hpp"

namespace hype::acpi {
    namespace {
        // MCFG: SDT header, 8 reserved bytes, then an array of 16-byte
        // segment-group descriptors.
        struct McfgHeader {
            SdtHeader sdt;
            hcpp::u64 reserved;
        } __attribute__((packed));
        struct McfgEntry {
            hcpp::u64 base;
            hcpp::u16 segment;
            hcpp::u8 bus_start;
            hcpp::u8 bus_end;
            hcpp::u32 reserved;
        } __attribute__((packed));
        static_assert(sizeof(McfgEntry) == 16);

        McfgInfo current;
        bool initialized;
    }  // namespace

    void mcfg_initialize() noexcept {
        current = {};
        initialized = true;
        // pci.scan (priority 20) runs before acpi.discover (priority
        // 40), so the ACPI root tables may not be up yet — bring them
        // up lazily. acpi::initialize() is idempotent, so the later
        // stage re-scan is harmless.
        if (!info().present) initialize();
        const auto* mcfg = reinterpret_cast<const McfgHeader*>(find_table("MCFG"));
        if (mcfg != nullptr && mcfg->sdt.length >= sizeof(McfgHeader) + sizeof(McfgEntry)) {
            current.present = true;
            const hcpp::size_t entries = (mcfg->sdt.length - sizeof(McfgHeader)) / sizeof(McfgEntry);
            const auto* list =
                reinterpret_cast<const McfgEntry*>(reinterpret_cast<const hcpp::u8*>(mcfg) + sizeof(McfgHeader));
            for (hcpp::size_t i = 0; i < entries && current.segment_count < mcfg_max_segments; ++i) {
                current.segments[current.segment_count++] = {list[i].base, list[i].segment, list[i].bus_start,
                                                             list[i].bus_end};
            }
            // The boot page tables only identity-map the low 4 GiB, so
            // a default ECAM window at or above 4 GiB is unreachable:
            // keep the table but mark ECAM unusable (CF8/CFC fallback).
            current.usable = current.segment_count != 0 && current.segments[0].base < 0x100000000ull;
        }
        serial::write("mcfg segs=");
        serial::write_dec32(static_cast<hcpp::u32>(current.segment_count));
        serial::write("\r\n");
    }

    const McfgInfo& mcfg_info() noexcept {
        if (!initialized) mcfg_initialize();
        return current;
    }

    const McfgSegment* mcfg_segment_for_bus(hcpp::u8 bus) noexcept {
        if (!initialized) mcfg_initialize();
        if (!current.usable) return nullptr;
        for (hcpp::size_t i = 0; i < current.segment_count; ++i) {
            const McfgSegment& segment = current.segments[i];
            if (segment.base < 0x100000000ull && bus >= segment.bus_start && bus <= segment.bus_end)
                return &segment;
        }
        return nullptr;
    }

}  // namespace hype::acpi
