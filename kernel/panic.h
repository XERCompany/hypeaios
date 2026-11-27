#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype {

    // Register snapshot assembled by the common ISR stub
    // (kernel/interrupts/idt_stubs.asm). Field order matches the
    // stub layout: saved GPRs, then vector, error code and the
    // CPU-pushed frame (rip/cs/rflags/rsp/ss).
    struct InterruptRegisters {
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

    const char* exception_name(hcpp::u64 vector) noexcept;

    // Optional: once a framebuffer is bound, panic also paints a
    // simple crash screen. The serial dump always happens.
    void panic_bind_framebuffer(const hcpp::graphics::Framebuffer& framebuffer) noexcept;

    [[noreturn]] void panic(hcpp::StringView reason) noexcept;
    [[noreturn]] void panic_exception(const InterruptRegisters& regs) noexcept;

    // Code-path selftest only: verifies the exception name table.
    // A live fault cannot be triggered because panic never returns.
    bool panic_selftest() noexcept;

}  // namespace hype
