#pragma once

#include "hcpp/types.hpp"

namespace hype::guard {

    // MtsFrame64 — machine trap state, 64-bit variant: the saved
    // context a guard gate (nsfguard.asm / mts.S) marshals around a
    // policy check. Field order matches kernel/panic.h
    // InterruptRegisters, which matches the push order of the common
    // ISR stub (kernel/interrupts/idt_stubs.asm): 15 saved GPRs, then
    // vector + error code, then the CPU-pushed frame
    // (rip/cs/rflags/rsp/ss). A guard hook handed a live trap can
    // therefore reinterpret the IDT frame as an MtsFrame64 directly.

    struct MtsFrame64 {
        hcpp::u64 r15;
        hcpp::u64 r14;
        hcpp::u64 r13;
        hcpp::u64 r12;
        hcpp::u64 r11;
        hcpp::u64 r10;
        hcpp::u64 r9;
        hcpp::u64 r8;
        hcpp::u64 rbp;
        hcpp::u64 rdi;
        hcpp::u64 rsi;
        hcpp::u64 rdx;
        hcpp::u64 rcx;
        hcpp::u64 rbx;
        hcpp::u64 rax;
        hcpp::u64 vector;
        hcpp::u64 error;
        hcpp::u64 rip;
        hcpp::u64 cs;
        hcpp::u64 rflags;
        hcpp::u64 rsp;
        hcpp::u64 ss;
    };
    static_assert(sizeof(MtsFrame64) == 22 * 8);

}  // namespace hype::guard

// NASM helpers (kernel/guard/mts.S). Not namespaced: they are the
// stable C ABI the asm side and other gates link against.
//
// mts_save_state(out, vector, error): captures the live GPRs into
// out->r15..rax, stores vector/error, records the return address as
// rip, the caller's rsp as rsp, and the live cs/ss/rflags.
// mts_restore_state(frame): restores r15..rax from the frame
// (GPRs only — rip/rsp/cs/ss/rflags describe the *saved* context and
// are not re-applied; use it for context inspection or switch-style
// partial restores).
extern "C" void mts_save_state(hype::guard::MtsFrame64* out,
                               hcpp::u64 vector, hcpp::u64 error) noexcept;
extern "C" void mts_restore_state(
    const hype::guard::MtsFrame64* frame) noexcept;
