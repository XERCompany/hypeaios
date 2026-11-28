#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Digigram LX6464ES 64-channel EtherSound interface.
// The board presents a PLX 9056 bridge (10B5:9056) and is only an
// LX6464ES when the subsystem pair is Digigram 1369:c001-c022 —
// matching the ALSA snd-lx6464es pci_device_id table exactly.
//
// Register map mirrors Linux ALSA sound/pci/lx6464es:
//  - BAR1 is the PLX local register block (mailbox/doorbell space)
//  - BAR2 is the Xilinx/MicroBlaze DSP register window
//
// Working: subsystem-verified identify, BAR1+BAR2 claim, the
// documented Xilinx reset sequence (CHIPSC RESET_XILINX toggle +
// MBOX3 done flag) and the MicroBlaze access test (eReg_CSM==0
// with the PCICR SPACE0_REMAP retry).
// Offline: EtherSound channel config (eReg_CONFES), the RMH
// command protocol and pipe/stream DMA — engine stays offline.
// Note: the issue sketch suggested 1369:0011; ALSA matches the
// PLX vendor:device pair plus Digigram subsystem ids, which is
// what this driver implements.

namespace hype::audio::lx6464es {

    constexpr hcpp::u16 vendor_plx = 0x10b5;
    constexpr hcpp::u16 device_plx9056 = 0x9056;
    constexpr hcpp::u16 subvendor_digigram = 0x1369;
    // Subsystem ids from the ALSA snd-lx6464es table.
    constexpr hcpp::u16 sub_lx6464es = 0xc001;
    constexpr hcpp::u16 sub_lx6464es_cae = 0xc002;
    constexpr hcpp::u16 sub_lx6464ese = 0xc021;
    constexpr hcpp::u16 sub_lx6464ese_cae = 0xc022;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;
    constexpr hcpp::u16 pci_subsystem_id_reg = 0x2c;

    // PLX local register offsets inside BAR1 (plx_port_offsets[]).
    constexpr hcpp::u32 plx_pcicr = 0x04;      // PCI control / remap
    constexpr hcpp::u32 plx_mbox0 = 0x40;
    constexpr hcpp::u32 plx_mbox1 = 0x44;
    constexpr hcpp::u32 plx_mbox2 = 0x48;
    constexpr hcpp::u32 plx_mbox3 = 0x4c;      // xilinx reset done flag
    constexpr hcpp::u32 plx_mbox4 = 0x50;
    constexpr hcpp::u32 plx_mbox5 = 0x54;
    constexpr hcpp::u32 plx_mbox6 = 0x58;
    constexpr hcpp::u32 plx_mbox7 = 0x5c;
    constexpr hcpp::u32 plx_l2pcidb = 0x64;    // local->pci doorbell
    constexpr hcpp::u32 plx_irqcs = 0x68;      // irq control/status
    constexpr hcpp::u32 plx_chipsc = 0x6c;     // chip status/control

    constexpr hcpp::u32 chipsc_reset_xilinx = 0x00010000;
    constexpr hcpp::u32 pcicr_space0_remap = 0x00000001;

    // DSP register indices into BAR2 — the window holds the
    // eReg_* register file at word index 0x400+ (dsp_port_offsets).
    constexpr hcpp::u32 dsp_csm = 0x400 * 4;        // command/status
    constexpr hcpp::u32 dsp_crm1 = 0x401 * 4;       // command regs
    constexpr hcpp::u32 dsp_icr = 0x410 * 4;        // interrupt ctrl
    constexpr hcpp::u32 dsp_isr = 0x412 * 4;        // interrupt status
    constexpr hcpp::u32 dsp_resetdsp = 0x416 * 4;   // dsp reset
    constexpr hcpp::u32 dsp_csuf = 0x420 * 4;       // status up flags
    constexpr hcpp::u32 dsp_cses = 0x430 * 4;       // ethersound status
    constexpr hcpp::u32 dsp_confes = 0x440 * 4;     // ethersound config

    constexpr hcpp::u32 csm_mc = 0x00000001;        // message command
    constexpr hcpp::u32 csm_mr = 0x00000002;        // message reply
    constexpr hcpp::u32 cses_es_ready = 0x00000004; // ethersound kicked

    struct Lx6464esController {
        hcpp::uintptr_t plx;    // BAR1 mapped base
        hcpp::uintptr_t dsp;    // BAR2 mapped base
        hcpp::u16 subsystem;
        bool reset_done;
        bool engine_ready;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *lx6464es_find() noexcept;
    bool lx6464es_bind(const pci::PciDevice &device,
                       Lx6464esController &controller) noexcept;
    bool lx6464es_reset(Lx6464esController &controller) noexcept;
    bool lx6464es_present() noexcept;
    bool lx6464es_probe() noexcept;

}  // namespace hype::audio::lx6464es
