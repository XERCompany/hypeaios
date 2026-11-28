#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/mts64.h"

namespace hype::guard {

    // Mts — machine trap/syscall state: the saved-context structs the
    // guard's asm gates (mts.S, nsfguard.asm, nfuip.S) marshal around a
    // policy check. The 64-bit frame lives in mts64.h and mirrors the
    // live IDT trap layout; this header carries the 32-bit sibling
    // (kept for compat-mode introspection and smaller dumps) plus the
    // trap-source tags the gates stamp into event records.

    // 32-bit variant, pushad order + CPU frame — matches the classic
    // x86 trap frame layout the long-mode entry path would synthesize
    // for a compat-mode task.
    struct MtsFrame32 {
        hcpp::u32 edi;
        hcpp::u32 esi;
        hcpp::u32 ebp;
        hcpp::u32 esp_ignored;  // esp as pushed by pushad (pre-push value)
        hcpp::u32 ebx;
        hcpp::u32 edx;
        hcpp::u32 ecx;
        hcpp::u32 eax;
        hcpp::u32 vector;
        hcpp::u32 error;
        hcpp::u32 eip;
        hcpp::u32 cs;
        hcpp::u32 eflags;
        hcpp::u32 esp;
        hcpp::u32 ss;
    };
    static_assert(sizeof(MtsFrame32) == 15 * 4);

    // Where a captured frame came from. Stamped into aux bits of gate
    // calls so the event record can tell a live trap from a synthetic
    // syscall-style gate.
    enum class MtsSource : hcpp::u8 {
        None = 0,
        IdtTrap,      // reinterpreted InterruptRegisters
        NsfGate,      // nsfguard_entry trampoline
        NfuipGate,    // nfuip_gate net-op entry
        Synthetic,    // built by software for a check
    };

    // Narrow helpers shared by the gates: does this frame describe a
    // supervisor-context trap (cs.RPL == 0)? Used by hooks that only
    // mediate user-context events.
    constexpr bool mts_kernel_context(const MtsFrame64& frame) noexcept {
        return (frame.cs & 0x3ull) == 0;
    }
    constexpr bool mts_kernel_context(const MtsFrame32& frame) noexcept {
        return (frame.cs & 0x3u) == 0;
    }

}  // namespace hype::guard
