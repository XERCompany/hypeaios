#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Echo Digital Audio cards (Darla/Gina/Layla/Mona/Mia/3G/Indigo).
// PCI ids mirror Linux ALSA sound/pci/echoaudio/*: the cards show
// up as Motorola DSP parts — vendor 0x1057 with device 0x1801
// (DSP56301 based) or 0x3410 (DSP56361 based); the board family is
// told apart by the subsystem id (subvendor 0xECC0).
//
// Working: identify (device + subsystem-name decode), BAR0 claim.
// Offline: everything past the BAR — these cards need the loader
// DSP firmware plus per-card DSP/ASIC images (ea/*.fw) uploaded
// through the comm page before they can do anything, and we ship
// no firmware blobs. engine_ready stays false by design.

namespace hype::audio::echoaudio {

    constexpr hcpp::u16 vendor_motorola = 0x1057;
    constexpr hcpp::u16 device_dsp56301 = 0x1801;
    constexpr hcpp::u16 device_dsp56361 = 0x3410;
    constexpr hcpp::u16 subvendor_ecc0 = 0xecc0;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;
    constexpr hcpp::u16 pci_subsystem_id_reg = 0x2c;

    struct EchoController {
        hcpp::uintptr_t mmio;
        hcpp::u16 device_id;
        hcpp::u16 subsystem;
        const char *board_name;
        bool reset_done;
        bool engine_ready;
    };

    const pci::PciDevice *echoaudio_find() noexcept;
    bool echoaudio_bind(const pci::PciDevice &device,
                        EchoController &controller) noexcept;
    bool echoaudio_reset(EchoController &controller) noexcept;
    const char *echoaudio_board_name(hcpp::u16 subsystem) noexcept;
    bool echoaudio_present() noexcept;
    bool echoaudio_probe() noexcept;

}  // namespace hype::audio::echoaudio
