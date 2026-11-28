#pragma once

// ALi M5451 PCI audio accelerator — PCI identity and config-space
// glue. The chip is a 32-voice wavetable/PCM engine with an AC'97
// front end; it depends on the M1533 southbridge (AC-link power) and
// the M7101 power-management unit for clock/reset sequencing.
//
// Reference: Linux sound/pci/ali5451/ali5451.c pci table and
// snd_ali_reset_5451() / snd_ali_proc_init() config-space access.

#include "hcpp/types.hpp"

namespace hype::chips::ali5451 {

    // ---- PCI identities --------------------------------------------
    constexpr hcpp::u16 pci_vendor_ali = 0x10B9;
    constexpr hcpp::u16 pci_dev_m5451 = 0x5451;      // audio controller
    constexpr hcpp::u16 pci_dev_m1533 = 0x1533;      // southbridge
    constexpr hcpp::u16 pci_dev_m7101 = 0x7101;      // power mgmt unit
    constexpr hcpp::u16 pci_dev_m5455 = 0x5455;      // plain AC'97 (not APU)
    constexpr hcpp::u16 pci_dev_m1535 = 0x1535;

    // PCI class: multimedia audio (04/01).
    constexpr hcpp::u8 pci_class_multimedia = 0x04;
    constexpr hcpp::u8 pci_subclass_audio = 0x01;

    // ---- Config-space registers ------------------------------------
    // M1533 (southbridge) dword 0x7C: bit 27 controls AC97 slot
    // power/reset routing; set then clear with ~5 ms delays.
    constexpr hcpp::u16 cfg_m1533_ac97 = 0x7C;
    constexpr hcpp::u32 m1533_ac97_enable = 0x08000000;

    // M5451 config dword 0x44 (legacy control): bits 19:18 toggle the
    // hardware reset sequence (write 0b11 then clear bit 18).
    constexpr hcpp::u16 cfg_m5451_legacy = 0x44;
    constexpr hcpp::u32 m5451_reset_toggle = 0x000C0000;
    constexpr hcpp::u32 m5451_reset_clear_mask = 0xFFFBFFFF; // ~bit18

    // Legacy-enable bits inside cfg 0x44 (FM/MPU/SB compatibility
    // decoders). Used to fold SB16/MPU401 emulation into the BAR.
    constexpr hcpp::u32 legacy_sb_enable = 0x00000001;
    constexpr hcpp::u32 legacy_fm_enable = 0x00000002;
    constexpr hcpp::u32 legacy_mpu_enable = 0x00000004;
    constexpr hcpp::u32 legacy_joystick = 0x00000008;

    // BAR0 = I/O port window used by all ali5451 registers
    // (ali5451-dev.h). BAR length is 256 bytes in current silicon.
    constexpr hcpp::u8 bar_io = 0;
    constexpr hcpp::u16 io_window_size = 0x100;

    // Silicon revisions observed in the field (PCI_REVISION_ID).
    constexpr hcpp::u8 rev_v01 = 0x01;
    constexpr hcpp::u8 rev_v02 = 0x02;   // ALI_5451_V02 quirks apply
    constexpr hcpp::u8 rev_e2 = 0xE2;
    constexpr hcpp::u8 rev_e4 = 0xE4;

    // Power state changes inside cfg 0x44 for V02+ parts (extra
    // bit 12 toggles during the codec-poke path, see dev header).
    constexpr hcpp::u32 m5451_v02_codec_flag = 0x0100;

    struct Ali5451Pci {
        hcpp::u8 bus;
        hcpp::u8 slot;
        hcpp::u8 function;
        hcpp::u8 revision;
        hcpp::u16 iobase;           // BAR0 I/O port base
        bool has_m1533;             // southbridge present at 10B9:1533
        bool has_m7101;             // PMU present at 10B9:7101
    };

    inline bool ali5451_is_apu(hcpp::u16 vendor, hcpp::u16 device) noexcept {
        return vendor == pci_vendor_ali && device == pci_dev_m5451;
    }

    // V02 parts need the extra 0x100 bit on codec write commands.
    inline bool ali5451_v02(hcpp::u8 revision) noexcept {
        return revision == rev_v02;
    }

}  // namespace hype::chips::ali5451
