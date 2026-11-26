#include "kernel/acpi/apic.h"

namespace hype::acpi {
    namespace {
        ApicInfo current;
        void cpuid(hcpp::u32 leaf, hcpp::u32& eax, hcpp::u32& ebx, hcpp::u32& ecx, hcpp::u32& edx) noexcept {
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0));
        }
        hcpp::u64 read_msr(hcpp::u32 msr) noexcept {
            hcpp::u32 low, high;
            asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
            return (static_cast<hcpp::u64>(high) << 32) | low;
        }
    }  // namespace

    void apic_initialize() noexcept {
        hcpp::u32 eax, ebx, ecx, edx;
        cpuid(1, eax, ebx, ecx, edx);
        current.cpuid_supported = (edx & (1u << 9)) != 0;
        current.x2apic_supported = (ecx & (1u << 21)) != 0;
        if (!current.cpuid_supported) return;
        const hcpp::u64 apic_base_msr = read_msr(0x1B);
        current.msr_enabled = (apic_base_msr & (1ull << 11)) != 0;
        current.apic_base = static_cast<hcpp::uintptr_t>(apic_base_msr & 0xFFFFF000ull);
    }
    const ApicInfo& apic_info() noexcept { return current; }

}  // namespace hype::acpi
