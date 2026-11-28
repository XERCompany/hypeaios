#include "kernel/power.h"

#include "kernel/acpi/fadt.h"
#include "kernel/serial.hpp"

namespace hype::power {

    namespace {

        void outw(hcpp::u16 port, hcpp::u16 value) noexcept {
            asm volatile("outw %0, %1" ::"a"(value), "Nd"(port));
        }

        void outl(hcpp::u16 port, hcpp::u32 value) noexcept {
            asm volatile("outl %0, %1" ::"a"(value), "Nd"(port));
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

        // Heuristic SLP_TYP used only when the DSDT has no \_S5 package
        // (fadt.s5_valid == false). The real values come from the \_S5
        // AML object parsed in kernel/acpi/fadt.cc; on QEMU/SeaBIOS the
        // package is {0,0,...} so this fallback never runs there. If a
        // platform's firmware expects a different value the write is
        // ignored and the fallback chain resumes.
        constexpr hcpp::u16 s5_slp_typ_fallback = 5;

        // PM1_CNT register layout (ACPI spec): SLP_TYP in bits 12:10,
        // SLP_EN in bit 13.
        constexpr hcpp::u16 pm1_slp_typ_shift = 10;
        constexpr hcpp::u16 pm1_slp_en = 1u << 13;

        // HW-reduced SLEEP_CONTROL_REG layout: SLP_TYP in bits 4:2,
        // SLP_EN in bit 5.
        constexpr hcpp::u8 slpctl_slp_typ_shift = 2;
        constexpr hcpp::u8 slpctl_slp_en = 1u << 5;

        // Issues an ACPI S5 (soft-off) write through the registers the
        // FADT reports. Returns true when at least one write was
        // dispatched; the machine powers off mid-write on success, so
        // reaching the return statement already means it was ignored.
        bool acpi_s5() noexcept {
            const acpi::FadtInfo& fadt = acpi::fadt_info();
            if (!fadt.present) {
                return false;
            }
            serial::write("acpi s5 pm1a=");
            serial::write_hex64(fadt.pm1a_cnt);
            serial::write(" pm1b=");
            serial::write_hex64(fadt.pm1b_cnt);
            serial::write(" hwreduced=");
            serial::write_line(fadt.hw_reduced ? "yes" : "no");

            const bool hw_path =
                fadt.hw_reduced && fadt.sleep_control_space == 1 &&
                fadt.sleep_control_addr != 0 &&
                fadt.sleep_control_addr <= 0xFFFF;
            const bool pm1_path = fadt.pm1a_cnt != 0 || fadt.pm1b_cnt != 0;
            if (!hw_path && !pm1_path) {
                return false;
            }
            const hcpp::u16 typ_a =
                fadt.s5_valid ? fadt.s5_typ_a : s5_slp_typ_fallback;
            const hcpp::u16 typ_b =
                fadt.s5_valid ? fadt.s5_typ_b : s5_slp_typ_fallback;
            serial::write("acpi s5 typ=");
            serial::write_dec32(typ_a);
            serial::write(fadt.s5_valid ? " dsdt\r\n" : " guess\r\n");
            // Nothing after a successful SLP_EN executes, so the "ok"
            // marker has to be committed before the port write.
            serial::write_line("power s5=ok");
            if (hw_path) {
                const hcpp::u16 port =
                    static_cast<hcpp::u16>(fadt.sleep_control_addr);
                const hcpp::u32 value =
                    (static_cast<hcpp::u32>(typ_a)
                     << slpctl_slp_typ_shift) |
                    slpctl_slp_en;
                if (fadt.sleep_control_width > 16) {
                    outl(port, value);
                } else if (fadt.sleep_control_width > 8) {
                    outw(port, static_cast<hcpp::u16>(value));
                } else {
                    serial::outb(port, static_cast<hcpp::u8>(value));
                }
            }
            if (fadt.pm1a_cnt != 0) {
                outw(fadt.pm1a_cnt,
                     static_cast<hcpp::u16>((typ_a << pm1_slp_typ_shift) |
                                            pm1_slp_en));
            }
            if (fadt.pm1b_cnt != 0) {
                outw(fadt.pm1b_cnt,
                     static_cast<hcpp::u16>((typ_b << pm1_slp_typ_shift) |
                                            pm1_slp_en));
            }
            return true;
        }

    }  // namespace

    [[noreturn]] void power_shutdown() noexcept {
        serial::write_line("power action=shutdown");
        // Ordered fallback chain:
        //   1. FADT-driven ACPI S5: SLEEP_CONTROL_REG on HW-reduced
        //      platforms (FADT revision >= 4), else PM1a_CNT_BLK and
        //      PM1b_CNT_BLK with the SLP_TYP=5 heuristic.
        //   2. Emulator pm-control ports 0x604/0xB004 for the case where
        //      the FADT was absent or the SLP_TYP guess was ignored.
        //   3. cli+hlt park.
        acpi_s5();
        serial::write_line("power s5=fallback");
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
        // Second mechanism: PCI reset-control register CF9h. Selecting
        // "hard" (0x02) then asserting it (0x06) resets the whole system
        // on most chipsets even when the 8042 line is unimplemented.
        serial::outb(0xCF9, 0x02);
        serial::outb(0xCF9, 0x06);
        // Last resort: force a triple fault by loading an empty IDT and
        // raising an exception.
        const IdtPointer empty_idt{0, 0};
        asm volatile("cli");
        asm volatile("lidt %0" ::"m"(empty_idt));
        asm volatile("int $3");
        halt_forever();
    }

}
