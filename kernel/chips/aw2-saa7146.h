#pragma once

// Philips SAA7146A multimedia bridge — register map as used by the
// Terratec EWS88/AW2 audio card. The SAA7146 provides PCI DMA, GPIO,
// two audio serial interfaces (AS1/AS2) with DDR bit-clock doubler,
// DEBI/GPIO-extension buses and the MC1/MC2 multimedia control regs.
//
// Reference: Linux sound/pci/aw2/saa7146.h (saa7146_core.h register
// list) and drivers/media/common/saa7146/saa7146_regs.c.

#include "hcpp/types.hpp"

namespace hype::chips::saa7146 {

    // ---- PCI / misc control ------------------------------------------
    constexpr hcpp::u32 reg_bctr = 0x00;          // Bus Control
    constexpr hcpp::u32 reg_btct = 0x04;          // Burst Transfer Ctrl
    constexpr hcpp::u32 reg_psr = 0x08;           // PCI Status
    constexpr hcpp::u32 reg_isr = 0x0C;           // Interrupt Status R
    constexpr hcpp::u32 reg_ier = 0x10;           // Interrupt Enable R
    constexpr hcpp::u32 reg_icr = 0x14;           // Interrupt Clear R

    // ---- DD1 (databus) init / GPIO ---------------------------------------
    constexpr hcpp::u32 reg_dd1_init = 0x50;
    constexpr hcpp::u32 reg_dd1_stream_b = 0x54;
    constexpr hcpp::u32 reg_dd1_config = 0x58;
    constexpr hcpp::u32 reg_gpio_ctrl = 0xE0;
    constexpr hcpp::u32 reg_ec1ssp = 0x84;        // ext. clk1 samp point
    constexpr hcpp::u32 reg_ec2ssp = 0x88;
    constexpr hcpp::u32 reg_ects1 = 0x8C;         // ext. clk trig sel 1
    constexpr hcpp::u32 reg_ects2 = 0x90;

    // ---- Audio DMA pointers ------------------------------------------------
    // Two audio DMA channels; each has a base/protect pair per
    // direction (A1 = ch1, A2 = ch2; "in" is capture, "out" playback).
    constexpr hcpp::u32 reg_base_a1_in = 0x94;
    constexpr hcpp::u32 reg_prot_a1_in = 0x98;
    constexpr hcpp::u32 reg_base_a1_out = 0x9C;
    constexpr hcpp::u32 reg_base_a2_in = 0xA0;
    constexpr hcpp::u32 reg_prot_a2_in = 0xA4;
    constexpr hcpp::u32 reg_base_a2_out = 0xA8;
    constexpr hcpp::u32 reg_base_a2_outp = 0xAC;  // protect alias
    constexpr hcpp::u32 reg_base_b1 = 0xB0;       // video DMA (unused)
    constexpr hcpp::u32 reg_base_b2 = 0xB4;
    constexpr hcpp::u32 reg_prot_a1_out = 0xB8;

    // ---- PCI address modifiers -------------------------------------------------
    constexpr hcpp::u32 reg_pci_adp1 = 0x12C;    // DMA ch1 addr mod
    constexpr hcpp::u32 reg_pci_adp2 = 0x130;    // DMA ch2 addr mod
    constexpr hcpp::u32 reg_pci_adp3 = 0x134;    // video DMA
    constexpr hcpp::u32 reg_pci_adp4 = 0x138;    // audio extra
    constexpr hcpp::u32 reg_ddp = 0x13C;         // DDR doubler param

    // ---- Audio serial port control ------------------------------------------------
    constexpr hcpp::u32 reg_acon1 = 0xF4;        // audio config 1
    constexpr hcpp::u32 reg_acon2 = 0xF8;        // audio config 2
    constexpr hcpp::u32 reg_mc1 = 0xFC;          // multimedia ctrl 1
    constexpr hcpp::u32 reg_mc2 = 0x100;         // multimedia ctrl 2
    constexpr hcpp::u32 reg_rps_addr0 = 0x104;
    constexpr hcpp::u32 reg_rps_addr1 = 0x108;
    constexpr hcpp::u32 reg_isr2 = 0x10C;        // IRQ status (2nd)
    constexpr hcpp::u32 reg_psr2 = 0x110;        // pin status
    constexpr hcpp::u32 reg_ssr = 0x114;         // serial sync reset

    // ---- DEBI (peripheral bus) / I2C -------------------------------------------------
    constexpr hcpp::u32 reg_debi_cfg = 0x7C;
    constexpr hcpp::u32 reg_debi_xfer = 0x80;
    constexpr hcpp::u32 reg_i2c_ctrl = 0x8C;     // (shared w/ ects1 slot
    constexpr hcpp::u32 reg_i2c_xfer = 0x90;     //  on this datasheet)

    // ---- TSL (time-slot list) -----------------------------------------------------
    constexpr hcpp::u32 reg_tsl1 = 0x180;        // 16 u32 entries
    constexpr hcpp::u32 reg_tsl2 = 0x1C0;

    // ---- ISR/IER bit masks ----------------------------------------------------------
    constexpr hcpp::u32 irq_debi = 0x00040000;
    constexpr hcpp::u32 irq_i2c_done = 0x00080000;
    constexpr hcpp::u32 irq_a1_in = 0x00000800;  // audio DMA1 input
    constexpr hcpp::u32 irq_a1_out = 0x00000200; // audio DMA1 output
    constexpr hcpp::u32 irq_a2_in = 0x00080000;
    constexpr hcpp::u32 irq_a2_out = 0x00020000;
    constexpr hcpp::u32 irq_rps0 = 0x00000010;
    constexpr hcpp::u32 irq_rps1 = 0x00000020;
    constexpr hcpp::u32 irq_hfida = 0x00008000;  // half-FIFO audio

    // ---- MC1 bit fields ------------------------------------------------------------
    constexpr hcpp::u32 mc1_3v = 0x00000001;     // 3.3V audio I/F
    constexpr hcpp::u32 mc1_rldd = 0x00000004;   // reload DD1
    constexpr hcpp::u32 mc1_pll = 0x00000008;
    constexpr hcpp::u32 mc1_fastest = 0x00000010;
    constexpr hcpp::u32 mc1_taps = 0x00000F00;   // time-slot alloc
    constexpr hcpp::u32 mc1_a1_sink = 0x00030000;
    constexpr hcpp::u32 mc1_a2_sink = 0x000C0000;

    // ---- MC2 bit fields -------------------------------------------------------------
    constexpr hcpp::u32 mc2_upld1 = 0x00000001;  // upload request A1
    constexpr hcpp::u32 mc2_upld2 = 0x00000002;  // upload request A2
    constexpr hcpp::u32 mc2_lowgain = 0x00000008;

    // ---- ACON1/ACON2 selected bits -------------------------------------------------
    constexpr hcpp::u32 acon1_a1_swap = 0x00000001;
    constexpr hcpp::u32 acon1_a2_swap = 0x00000002;
    constexpr hcpp::u32 acon1_bclk1_inv = 0x00000004;
    constexpr hcpp::u32 acon1_bclk2_inv = 0x00000008;
    constexpr hcpp::u32 acon1_mem_max = 0x000003F0;
    constexpr hcpp::u32 acon1_lfs1 = 0x00C00000; // left-justified fmt
    constexpr hcpp::u32 acon1_dsd_en = 0x40000000;
    constexpr hcpp::u32 acon2_a1_clk2x = 0x00000001;  // DDR clock on A1
    constexpr hcpp::u32 acon2_a2_clk2x = 0x00000002;
    constexpr hcpp::u32 acon2_tsl_rdy = 0x00000080;

    // ---- GPIO_CTRL bits --------------------------------------------------------------
    constexpr hcpp::u32 gpio_ddr_hi = 0x00001000;
    constexpr hcpp::u32 gpio_ddr_lo = 0x00002000;
    constexpr hcpp::u32 gpio_in_shift = 0;

    // MMIO access helpers (SAA7146 registers are 32-bit BAR0).
    inline volatile hcpp::u32 *reg_ptr(hcpp::uintptr_t bar,
                                       hcpp::u32 off) noexcept {
        return reinterpret_cast<volatile hcpp::u32 *>(bar + off);
    }
    inline hcpp::u32 read(hcpp::uintptr_t bar, hcpp::u32 off) noexcept {
        return *reg_ptr(bar, off);
    }
    inline void write(hcpp::uintptr_t bar, hcpp::u32 off,
                      hcpp::u32 v) noexcept {
        *reg_ptr(bar, off) = v;
    }

}  // namespace hype::chips::saa7146
