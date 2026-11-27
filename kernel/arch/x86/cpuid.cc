#include "kernel/arch/x86/cpuid.h"

namespace hype::x86 {

    namespace {

        CpuFeatures current;
        bool initialized = false;

        void cpuid(hcpp::u32 leaf, hcpp::u32 subleaf, hcpp::u32 &eax, hcpp::u32 &ebx,
                   hcpp::u32 &ecx, hcpp::u32 &edx) noexcept {
            asm volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(leaf), "c"(subleaf));
        }

        bool bit(hcpp::u32 value, hcpp::u8 index) noexcept {
            return (value & (1u << index)) != 0;
        }

    }  // namespace

    void cpuid_initialize() noexcept {
        if (initialized) {
            return;
        }
        hcpp::u32 eax, ebx, ecx, edx;

        // Leaf 0: max basic leaf + 12-char vendor in EBX/EDX/ECX order.
        cpuid(0, 0, eax, ebx, ecx, edx);
        current.max_leaf = eax;
        current.vendor[0] = static_cast<char>(ebx & 0xFF);
        current.vendor[1] = static_cast<char>((ebx >> 8) & 0xFF);
        current.vendor[2] = static_cast<char>((ebx >> 16) & 0xFF);
        current.vendor[3] = static_cast<char>((ebx >> 24) & 0xFF);
        current.vendor[4] = static_cast<char>(edx & 0xFF);
        current.vendor[5] = static_cast<char>((edx >> 8) & 0xFF);
        current.vendor[6] = static_cast<char>((edx >> 16) & 0xFF);
        current.vendor[7] = static_cast<char>((edx >> 24) & 0xFF);
        current.vendor[8] = static_cast<char>(ecx & 0xFF);
        current.vendor[9] = static_cast<char>((ecx >> 8) & 0xFF);
        current.vendor[10] = static_cast<char>((ecx >> 16) & 0xFF);
        current.vendor[11] = static_cast<char>((ecx >> 24) & 0xFF);
        current.vendor[12] = '\0';

        // Leaf 1: family/model/stepping + feature bitmaps.
        if (current.max_leaf >= 1) {
            cpuid(1, 0, eax, ebx, ecx, edx);
            current.leaf1_ecx = ecx;
            current.leaf1_edx = edx;
            current.stepping = static_cast<hcpp::u8>(eax & 0xF);
            const hcpp::u32 base_model = (eax >> 4) & 0xF;
            const hcpp::u32 base_family = (eax >> 8) & 0xF;
            current.type = static_cast<hcpp::u8>((eax >> 12) & 0x3);
            const hcpp::u32 ext_model = (eax >> 16) & 0xF;
            const hcpp::u32 ext_family = (eax >> 20) & 0xFF;
            current.family =
                static_cast<hcpp::u8>(base_family == 0xF ? base_family + ext_family
                                                        : base_family);
            current.model = static_cast<hcpp::u8>(
                (base_family == 0x6 || base_family == 0xF)
                    ? base_model + (ext_model << 4)
                    : base_model);
        }

        // Leaf 7 subleaf 0: extended feature bits (AVX2 and friends).
        if (current.max_leaf >= 7) {
            cpuid(7, 0, eax, ebx, ecx, edx);
            current.leaf7_ebx = ebx;
        }

        // Extended leaves: max index, then feature/APM bits.
        cpuid(0x80000000, 0, eax, ebx, ecx, edx);
        current.max_ext_leaf = eax;
        if (current.max_ext_leaf >= 0x80000001) {
            cpuid(0x80000001, 0, eax, ebx, ecx, edx);
            current.ext1_ecx = ecx;
            current.ext1_edx = edx;
        }
        if (current.max_ext_leaf >= 0x80000007) {
            cpuid(0x80000007, 0, eax, ebx, ecx, edx);
            current.ext7_edx = edx;
        }

        initialized = true;
    }

    const CpuFeatures &cpuid_features() noexcept {
        cpuid_initialize();
        return current;
    }

    bool cpuid_has(CpuFeature feature) noexcept {
        cpuid_initialize();
        switch (feature) {
            case CpuFeature::Mmx:
                return bit(current.leaf1_edx, 23);
            case CpuFeature::Sse:
                return bit(current.leaf1_edx, 25);
            case CpuFeature::Sse2:
                return bit(current.leaf1_edx, 26);
            case CpuFeature::Sse3:
                return bit(current.leaf1_ecx, 0);
            case CpuFeature::Ssse3:
                return bit(current.leaf1_ecx, 9);
            case CpuFeature::Sse41:
                return bit(current.leaf1_ecx, 19);
            case CpuFeature::Sse42:
                return bit(current.leaf1_ecx, 20);
            case CpuFeature::Avx:
                return bit(current.leaf1_ecx, 28);
            case CpuFeature::Avx2:
                return bit(current.leaf7_ebx, 5);
            case CpuFeature::X2Apic:
                return bit(current.leaf1_ecx, 21);
            case CpuFeature::XSave:
                return bit(current.leaf1_ecx, 26);
            case CpuFeature::TscDeadline:
                return bit(current.leaf1_ecx, 24);
            case CpuFeature::Hypervisor:
                return bit(current.leaf1_ecx, 31);
            case CpuFeature::Nx:
                return bit(current.ext1_edx, 20);
            case CpuFeature::LongMode:
                return bit(current.ext1_edx, 29);
            case CpuFeature::InvariantTsc:
                return bit(current.ext7_edx, 8);
            case CpuFeature::Count:
                break;
        }
        return false;
    }

    bool cpuid_selftest() noexcept {
        cpuid_initialize();
        return current.vendor[0] != '\0' && cpuid_has(CpuFeature::Sse2) &&
               cpuid_has(CpuFeature::LongMode) && cpuid_has(CpuFeature::Nx);
    }

}  // namespace hype::x86
