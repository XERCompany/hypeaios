#pragma once

#include "hcpp/types.hpp"

namespace hype::power {

    // Prints "power action=shutdown", then tries the QEMU/Bochs ACPI
    // power-off port (0x604) and the older PIIX4 port (0xB004). When no
    // emulator honors the write the CPU is parked in a cli+hlt loop.
    [[noreturn]] void power_shutdown() noexcept;

    // Prints "power action=restart", pulses the 8042 keyboard-controller
    // reset line (outb 0x64, 0xFE) and falls back to a triple fault when
    // the controller does not reset the machine.
    [[noreturn]] void power_restart() noexcept;

}
