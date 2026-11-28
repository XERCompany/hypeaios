#pragma once

#include "hcpp/types.hpp"

namespace hype::power {

    // Prints "power action=shutdown", then attempts a real ACPI S5
    // (soft-off): the FADT's SLEEP_CONTROL_REG on HW-reduced platforms,
    // else PM1a/PM1b_CNT_BLK with SLP_EN|SLP_TYP. If the machine is
    // still alive after that it falls back to the QEMU/Bochs
    // power-off ports (0x604, 0xB004) and finally a cli+hlt loop.
    [[noreturn]] void power_shutdown() noexcept;

    // Prints "power action=restart", pulses the 8042 keyboard-controller
    // reset line (outb 0x64, 0xFE), then tries the PCI reset-control
    // register (port 0xCF9) and finally a triple fault when neither
    // mechanism resets the machine.
    [[noreturn]] void power_restart() noexcept;

}
