#pragma once

#include "hcpp/types.hpp"

namespace hype::acpi {

    // One MCFG allocation: a PCI segment group whose configuration
    // space is reachable through a memory-mapped ECAM window covering
    // buses [bus_start, bus_end].
    struct McfgSegment {
        hcpp::u64 base;
        hcpp::u16 segment;
        hcpp::u8 bus_start;
        hcpp::u8 bus_end;
    };

    constexpr hcpp::size_t mcfg_max_segments = 8;

    struct McfgInfo {
        bool present;   // an MCFG table was found and parsed
        bool usable;    // the default (first) segment base is < 4 GiB,
                        // i.e. covered by the boot identity map
        hcpp::size_t segment_count;
        McfgSegment segments[mcfg_max_segments];
    };

    // Parses the MCFG table. Safe to call before acpi::initialize():
    // when the ACPI root tables have not been located yet it brings
    // them up first (pci.scan runs before acpi.discover).
    void mcfg_initialize() noexcept;

    const McfgInfo& mcfg_info() noexcept;

    // The segment group covering `bus`, or nullptr when the bus is
    // outside every ECAM window (or ECAM is unusable — call sites then
    // fall back to the legacy CF8/CFC ports).
    const McfgSegment* mcfg_segment_for_bus(hcpp::u8 bus) noexcept;

}  // namespace hype::acpi
