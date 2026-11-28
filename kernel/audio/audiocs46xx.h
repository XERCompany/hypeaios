#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Cirrus Logic CrystalClear SoundFusion CS46xx
// (CS4610/CS4611/CS4614/CS4622/CS4624/CS4630/CS4280).
// Register map mirrors Linux ALSA sound/pci/cs46xx/cs46xx.h:
//  - BAR0 is the 4 KiB "BA0" control window (memory space)
//  - BAR1 is the "BA1" DSP program/parameter window
//  - AC'97 codec access goes through the ACCTL/ACCAD/ACCDA/ACSTS
//    handshake documented in snd_cs46xx_codec_read().
//
// Working: identify, BA0/BA1 claim, host-interrupt mask, serial
// port type select + AC'97 codec reset + master volume.
// Offline: the DSP image download — the SoundFusion DSP needs the
// CWCDSP firmware blob (cs46xx_image.h) and an SCB task tree
// before any audio flows, so engine_ready stays false.

namespace hype::audio::cs46xx {

    constexpr hcpp::u16 vendor_cirrus = 0x1013;
    constexpr hcpp::u16 device_cs4610 = 0x6001;
    constexpr hcpp::u16 device_cs4614 = 0x6003;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // BA0 (BAR0) register offsets — u32 unless noted.
    constexpr hcpp::u32 ba0_hisr = 0x000;      // host int status
    constexpr hcpp::u32 ba0_hsr0 = 0x004;      // host semaphore 0
    constexpr hcpp::u32 ba0_hicr = 0x008;      // host int control
    constexpr hcpp::u32 ba0_clkcr1 = 0x400;    // clock control 1
    constexpr hcpp::u32 ba0_pllm = 0x408;      // PLL mode
    constexpr hcpp::u32 ba0_pllcc = 0x40c;     // PLL coefficient
    constexpr hcpp::u32 ba0_frr = 0x410;       // feature reporting
    constexpr hcpp::u32 ba0_sermc1 = 0x420;    // serial port master ctl 1
    constexpr hcpp::u32 ba0_sermc2 = 0x424;    // serial port master ctl 2
    constexpr hcpp::u32 ba0_serc1 = 0x428;     // serial port config 1
    constexpr hcpp::u32 ba0_serc2 = 0x42c;     // serial port config 2
    constexpr hcpp::u32 ba0_acctl = 0x460;     // AC'97 control
    constexpr hcpp::u32 ba0_acsts = 0x464;     // AC'97 status
    constexpr hcpp::u32 ba0_acosv = 0x468;     // AC'97 out slot valid
    constexpr hcpp::u32 ba0_accad = 0x46c;     // AC'97 command address
    constexpr hcpp::u32 ba0_accda = 0x470;     // AC'97 command data
    constexpr hcpp::u32 ba0_acisv = 0x474;     // AC'97 in slot valid
    constexpr hcpp::u32 ba0_acsad = 0x478;     // AC'97 status address
    constexpr hcpp::u32 ba0_acsda = 0x47c;     // AC'97 status data
    constexpr hcpp::u32 ba0_acctl2 = 0x4e0;    // secondary codec ctl
    constexpr hcpp::u32 ba0_acsts2 = 0x4e4;    // secondary codec sts

    constexpr hcpp::u32 hicr_iev = 0x00000001; // interrupt enable
    constexpr hcpp::u32 hicr_chgm = 0x00000002;// host change mask

    constexpr hcpp::u32 acctl_rstn = 0x00000001; // codec not-in-reset
    constexpr hcpp::u32 acctl_esyn = 0x00000002;
    constexpr hcpp::u32 acctl_vfrm = 0x00000004; // valid frame
    constexpr hcpp::u32 acctl_dcv = 0x00000008;  // dynamic command valid
    constexpr hcpp::u32 acctl_crw = 0x00000010;  // command read/write
    constexpr hcpp::u32 acctl_asyn = 0x00000020;
    constexpr hcpp::u32 acctl_tc = 0x00000040;   // target codec select
    constexpr hcpp::u32 acsts_crdy = 0x00000001; // codec ready
    constexpr hcpp::u32 acsts_vsts = 0x00000002; // valid status

    constexpr hcpp::u32 sermc1_mspe = 0x00000001; // serial port enable
    constexpr hcpp::u32 sermc1_ptc_ac97 = 0x00000002; // port type AC'97

    // AC'97 codec registers.
    constexpr hcpp::u8 ac97_reset = 0x00;
    constexpr hcpp::u8 ac97_master_volume = 0x02;
    constexpr hcpp::u8 ac97_pcmout_volume = 0x18;
    constexpr hcpp::u8 ac97_vendor_id1 = 0x7c;
    constexpr hcpp::u8 ac97_vendor_id2 = 0x7e;

    constexpr hcpp::u8 ring_pages = 4;

    struct Cs46xxController {
        hcpp::uintptr_t ba0;   // BAR0 mapped base (identity map)
        hcpp::uintptr_t ba1;   // BAR1 DSP window base
        hcpp::u16 device_id;
        bool reset_done;
        bool engine_ready;
        hcpp::u16 codec_vendor;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *cs46xx_find() noexcept;
    bool cs46xx_bind(const pci::PciDevice &device,
                     Cs46xxController &controller) noexcept;
    bool cs46xx_reset(Cs46xxController &controller) noexcept;
    bool cs46xx_codec_write(Cs46xxController &controller, hcpp::u8 reg,
                            hcpp::u16 value) noexcept;
    bool cs46xx_present() noexcept;
    bool cs46xx_probe() noexcept;

}  // namespace hype::audio::cs46xx
