#include "kernel/panic.h"

#include "hcpp/graphics/text.hpp"
#include "kernel/arch/x86/cpu.h"
#include "kernel/log/crashdump.h"
#include "kernel/serial.hpp"

namespace hype {
    namespace {

        constexpr hcpp::u64 vector_page_fault = 14;
        constexpr hcpp::u64 pf_error_present = 1U << 0;
        constexpr hcpp::u64 pf_error_write = 1U << 1;
        constexpr hcpp::u64 pf_error_user = 1U << 2;
        constexpr hcpp::u64 pf_error_reserved = 1U << 3;
        constexpr hcpp::u64 pf_error_fetch = 1U << 4;

        const char* const exception_names[32] = {
            "#DE divide error",              // 0
            "#DB debug",                     // 1
            "NMI non-maskable interrupt",    // 2
            "#BP breakpoint",                // 3
            "#OF overflow",                  // 4
            "#BR bound range exceeded",      // 5
            "#UD invalid opcode",            // 6
            "#NM device not available",      // 7
            "#DF double fault",              // 8
            "coprocessor segment overrun",   // 9
            "#TS invalid tss",               // 10
            "#NP segment not present",       // 11
            "#SS stack segment fault",       // 12
            "#GP general protection fault",  // 13
            "#PF page fault",                // 14
            "reserved",                      // 15
            "#MF x87 floating-point error",  // 16
            "#AC alignment check",           // 17
            "#MC machine check",             // 18
            "#XM simd floating-point",       // 19
            "#VE virtualization",            // 20
            "#CP control protection",        // 21
            "reserved",                      // 22
            "reserved",                      // 23
            "reserved",                      // 24
            "reserved",                      // 25
            "reserved",                      // 26
            "reserved",                      // 27
            "reserved",                      // 28
            "#VC vmm communication",         // 29
            "#SX security",                  // 30
            "reserved",                      // 31
        };

        hcpp::graphics::Framebuffer panic_fb;
        volatile bool panic_active = false;

        void write_field(hcpp::StringView name, hcpp::u64 value) noexcept {
            serial::write(name);
            serial::write_char('=');
            serial::write_hex64(value);
        }

        [[noreturn]] void halt_loop() noexcept {
            hype_cpu_disable_interrupts();
            for (;;) {
                hype_cpu_halt();
            }
        }

        void panic_screen(hcpp::StringView line_a, hcpp::StringView line_b) noexcept {
            if (!panic_fb.valid()) {
                return;
            }
            const hcpp::u32 background = panic_fb.encode_rgb(12, 20, 140);
            const hcpp::u32 title = panic_fb.encode_rgb(240, 244, 255);
            const hcpp::u32 accent = panic_fb.encode_rgb(140, 200, 255);
            panic_fb.clear(background);
            hcpp::u32 y = 48;
            hcpp::graphics::draw_text(panic_fb, 48, y, "HYPEAI OS", title, 4);
            y += 64;
            hcpp::graphics::draw_text(panic_fb, 48, y, "KERNEL PANIC", accent, 3);
            y += 48;
            hcpp::graphics::draw_text(panic_fb, 48, y, line_a, title, 1);
            y += 20;
            hcpp::graphics::draw_text(panic_fb, 48, y, line_b, title, 1);
            y += 40;
            hcpp::graphics::draw_text(panic_fb, 48, y, "SYSTEM HALTED - SEE SERIAL LOG", accent, 1);
        }

        void dump_registers(const InterruptRegisters& regs) noexcept {
            write_field("rip", regs.rip);
            serial::write("  ");
            write_field("cs", regs.cs);
            serial::write("  ");
            write_field("rflags", regs.rflags);
            serial::write("\r\n");
            write_field("rsp", regs.rsp);
            serial::write("  ");
            write_field("ss", regs.ss);
            serial::write("\r\n");
            if (regs.vector == vector_page_fault) {
                write_field("cr2", hype_cpu_read_cr2());
                serial::write("  pf:");
                serial::write((regs.error & pf_error_present) != 0 ? "present" : "not-present");
                serial::write((regs.error & pf_error_write) != 0 ? " write" : " read");
                serial::write((regs.error & pf_error_user) != 0 ? " user" : " supervisor");
                if ((regs.error & pf_error_reserved) != 0) serial::write(" reserved-bit");
                if ((regs.error & pf_error_fetch) != 0) serial::write(" fetch");
                serial::write("\r\n");
            }
            write_field("rax", regs.rax);
            serial::write("  ");
            write_field("rbx", regs.rbx);
            serial::write("  ");
            write_field("rcx", regs.rcx);
            serial::write("  ");
            write_field("rdx", regs.rdx);
            serial::write("\r\n");
            write_field("rsi", regs.rsi);
            serial::write("  ");
            write_field("rdi", regs.rdi);
            serial::write("  ");
            write_field("rbp", regs.rbp);
            serial::write("\r\n");
            write_field("r8", regs.r8);
            serial::write("  ");
            write_field("r9", regs.r9);
            serial::write("  ");
            write_field("r10", regs.r10);
            serial::write("  ");
            write_field("r11", regs.r11);
            serial::write("\r\n");
            write_field("r12", regs.r12);
            serial::write("  ");
            write_field("r13", regs.r13);
            serial::write("  ");
            write_field("r14", regs.r14);
            serial::write("  ");
            write_field("r15", regs.r15);
            serial::write("\r\n");
        }

    }  // namespace

    const char* exception_name(hcpp::u64 vector) noexcept {
        return vector < 32 ? exception_names[vector] : "unknown";
    }

    void panic_bind_framebuffer(const hcpp::graphics::Framebuffer& framebuffer) noexcept {
        panic_fb = framebuffer;
    }

    [[noreturn]] void panic(hcpp::StringView reason) noexcept {
        hype_cpu_disable_interrupts();
        if (panic_active) {
            halt_loop();
        }
        panic_active = true;
        serial::write("\r\n*** KERNEL PANIC ***\r\nreason=");
        serial::write(reason);
        serial::write("\r\nsystem halted\r\n");
        // Persist the klog ring (U:/crash.log or the scratch LBA) before
        // the halt loop — issue #119. Bounded; cannot hang the path.
        crashdump_write("KERNEL PANIC", nullptr);
        panic_screen("KERNEL PANIC", reason);
        halt_loop();
    }

    [[noreturn]] void panic_exception(const InterruptRegisters& regs) noexcept {
        hype_cpu_disable_interrupts();
        if (panic_active) {
            halt_loop();
        }
        panic_active = true;
        serial::write("\r\n*** KERNEL PANIC ***\r\nexception vector=");
        serial::write_dec32(static_cast<hcpp::u32>(regs.vector));
        serial::write(" name=");
        serial::write(exception_name(regs.vector));
        serial::write("\r\n");
        write_field("error", regs.error);
        serial::write("\r\n");
        dump_registers(regs);
        serial::write("system halted\r\n");
        // Persist the klog ring + panic frame (issue #119): the serial
        // lines above already sit in the ring via the serial tap; this
        // writes the file/LBA copy before halting.
        crashdump_write("KERNEL PANIC", &regs);
        panic_screen("KERNEL PANIC", exception_name(regs.vector));
        halt_loop();
    }

    bool panic_selftest() noexcept {
        // Verify every architecturally defined exception vector has a
        // proper name wired to the panic path. A live fault cannot be
        // triggered here because panic never returns.
        const hcpp::u64 defined[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                     12, 13, 14, 16, 17, 18, 19, 20, 21, 29, 30};
        for (hcpp::size_t i = 0; i < sizeof(defined) / sizeof(defined[0]); ++i) {
            const char* name = exception_name(defined[i]);
            if (name == nullptr || name[0] == 'r') {
                return false;  // "reserved" is not a real exception name
            }
        }
        return true;
    }

}  // namespace hype
