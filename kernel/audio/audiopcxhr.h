#pragma once

// Digigram PCXHR (HR/HRX-PCI) family (issue #118, wave 2).
// Register map mirrors ALSA sound/pci/pcxhr/pcxhr_core.c.
//
// The board presents a PLX PCI bridge chip as its PCI function
// (10b5:9656 on HR boards, 10b5:9056 on the "E" generation) with the
// Digigram vendor id 0x1369 only in the subsystem vendor field; the
// issue's 1369:0010 guess is therefore corrected to the PLX primary id
// plus a 1369 subsystem-vendor match.
//
// BAR layout used by ALSA: BAR1 = PLX local regs (mailbox 0x40..0x5C,
// doorbell 0x64, IRQ ctrl 0x68, chip-select/serial 0x6C), BAR2 = DSP
// host interface (HI08).  The card needs a Xilinx bitstream plus DSP
// boot/main images before any audio runs -> firmware path.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::pcxhr {

    constexpr hcpp::u16 vendor_plx = 0x10B5;
    constexpr hcpp::u16 device_plx9656 = 0x9656;  // PCXHR HR boards
    constexpr hcpp::u16 device_plx9056 = 0x9056;  // PCXHR "E" boards
    constexpr hcpp::u16 subvendor_digigram = 0x1369;

    // PLX local registers (BAR1, u32 access).
    constexpr hcpp::u16 plx_mbox0 = 0x40;
    constexpr hcpp::u16 plx_mbox1 = 0x44;
    constexpr hcpp::u16 plx_l2pcidb = 0x64;
    constexpr hcpp::u16 plx_irqcs = 0x68;
    constexpr hcpp::u16 plx_chipsc = 0x6C;

    constexpr hcpp::u32 mbox0_hf5 = 1u << 0;
    constexpr hcpp::u32 mbox0_hf4 = 1u << 1;
    constexpr hcpp::u32 mbox0_boot_here = 1u << 23;

    constexpr hcpp::u32 irqcs_enable_pciirq = 1u << 8;
    constexpr hcpp::u32 irqcs_enable_pcidb = 1u << 9;
    constexpr hcpp::u32 irqcs_active_pcidb = 1u << 13;

    constexpr hcpp::u32 chipsc_init_value = 0x100D767E;
    constexpr hcpp::u32 chipsc_reset_xilinx = 1u << 16;
    constexpr hcpp::u32 chipsc_gpi_useri = 1u << 17;
    constexpr hcpp::u32 chipsc_data_clk = 1u << 24;
    constexpr hcpp::u32 chipsc_data_in = 1u << 26;

    // DSP HI08 host interface registers (BAR2, u8 access only).
    constexpr hcpp::u16 dsp_icr = 0x00;
    constexpr hcpp::u16 dsp_cvr = 0x04;
    constexpr hcpp::u16 dsp_isr = 0x08;
    constexpr hcpp::u16 dsp_ivr = 0x0C;
    constexpr hcpp::u16 dsp_rxh = 0x14;
    constexpr hcpp::u16 dsp_rxm = 0x18;
    constexpr hcpp::u16 dsp_rxl = 0x1C;
    constexpr hcpp::u16 dsp_txh = 0x14;
    constexpr hcpp::u16 dsp_txm = 0x18;
    constexpr hcpp::u16 dsp_txl = 0x1C;
    constexpr hcpp::u16 dsp_reset = 0x20;

    constexpr hcpp::u8 icr_hi08_init = 0x80;
    constexpr hcpp::u8 isr_hi08_rxdf = 0x01;
    constexpr hcpp::u8 isr_hi08_txde = 0x02;
    constexpr hcpp::u8 isr_hi08_trdy = 0x04;

    struct PcxhrController {
        hcpp::u16 plx;      // BAR1 I/O base
        hcpp::u16 dsp;      // BAR2 I/O base
        bool xilinx_present;
        bool boot_here;
        bool reset_done;
        bool fw_loaded;
    };

    const pci::PciDevice *pcxhr_find() noexcept;
    bool pcxhr_bind(const pci::PciDevice &device,
                    PcxhrController &controller) noexcept;
    bool pcxhr_reset(PcxhrController &controller) noexcept;
    bool pcxhr_load_xilinx(PcxhrController &controller, const void *image,
                           hcpp::u32 bytes) noexcept;
    bool pcxhr_present() noexcept;
    bool pcxhr_selftest() noexcept;

}  // namespace hype::audio::pcxhr
