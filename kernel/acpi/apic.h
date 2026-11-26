#pragma once

#include "hcpp/types.hpp"

namespace hype::acpi {

    struct ApicInfo {
        bool cpuid_supported;
        bool msr_enabled;
        bool x2apic_supported;
        hcpp::uintptr_t apic_base;
    };

    void apic_initialize() noexcept;
    const ApicInfo& apic_info() noexcept;

}  // namespace hype::acpi
