#include "kernel/power.h"

#include "kernel/serial.hpp"

namespace hype::power {

    namespace {

        void outw(hcpp::u16 port, hcpp::u16 value) noexcept {
            asm volatile("outw %0, %1" ::"a"(value), "Nd"(port));
        }

        [[noreturn]] void halt_forever() noexcept {
            for (;;) {
                asm volatile("cli");
                asm volatile("hlt");
            }
        }

        struct IdtPointer {
            hcpp::u16 limit;
            hcpp::u64 base;
        } __attribute__((packed));

    }  // namespace

    [[noreturn]] void power_shutdown() noexcept {
        serial::write_line("power action=shutdown");
        // QEMU/Bochs ACPI shutdown port (new-style pm-control), then the
        // legacy PIIX4 port. Either one exits the VM instantly when
        // present; on bare metal both writes are harmless no-ops.
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        serial::write_line("power shutdown=halt");
        halt_forever();
    }

    [[noreturn]] void power_restart() noexcept {
        serial::write_line("power action=restart");
        // 8042 "pulse CPU reset line" command; wait for the input buffer
        // to drain first so the command is not dropped.
        for (hcpp::u32 spins = 0; spins < 100000; ++spins) {
            if ((serial::inb(0x64) & 0x02) == 0) {
                break;
            }
        }
        serial::outb(0x64, 0xFE);
        // Fallback: force a triple fault by loading an empty IDT and
        // raising an exception.
        const IdtPointer empty_idt{0, 0};
        asm volatile("cli");
        asm volatile("lidt %0" ::"m"(empty_idt));
        asm volatile("int $3");
        halt_forever();
    }

}
