#pragma once

#include "hcpp/types.hpp"

namespace hype::x86 {

    enum class CpuFeature : hcpp::u8 {
        Mmx,
        Sse,
        Sse2,
        Sse3,
        Ssse3,
        Sse41,
        Sse42,
        Avx,
        Avx2,
        X2Apic,
        XSave,
        TscDeadline,
        Hypervisor,
        Nx,
        LongMode,
        InvariantTsc,
        Count
    };

    struct CpuFeatures {
        char vendor[13];         // leaf 0 vendor string, NUL-terminated
        hcpp::u32 max_leaf;      // highest supported basic leaf
        hcpp::u32 max_ext_leaf;  // highest supported extended leaf
        hcpp::u8 stepping;
        hcpp::u8 model;
        hcpp::u8 family;
        hcpp::u8 type;
        hcpp::u32 leaf1_ecx;     // leaf 1 feature bits
        hcpp::u32 leaf1_edx;
        hcpp::u32 leaf7_ebx;     // leaf 7 subleaf 0 feature bits (0 if absent)
        hcpp::u32 ext1_ecx;      // leaf 0x80000001 feature bits (0 if absent)
        hcpp::u32 ext1_edx;
        hcpp::u32 ext7_edx;      // leaf 0x80000007 EDX (invariant TSC, 0 if absent)
    };

    // Runs CPUID leaves 0, 1, 7 and 0x80000000-0x80000007 once and caches
    // the results. Safe to call repeatedly; cheap after the first call.
    void cpuid_initialize() noexcept;

    const CpuFeatures &cpuid_features() noexcept;

    // Queries a feature bit from the cached CpuFeatures. Initializes on
    // first use so callers do not need explicit ordering.
    bool cpuid_has(CpuFeature feature) noexcept;

    // Cheap self-check: vendor string present plus the SSE2/long-mode
    // bits every x86-64 CPU must report.
    bool cpuid_selftest() noexcept;

}  // namespace hype::x86
