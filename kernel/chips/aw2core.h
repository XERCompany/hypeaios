#pragma once

// Terratec AW2 core — chip-level state, DMA bookkeeping and the
// initialisation word sequence the driver programs into the SAA7146
// to bring the audio serial ports up in I2S mode at 44.1 kHz.
//
// Reference: Linux sound/pci/aw2/aw2-alsa.c (probe + hw params) and
// aw2-saa7146.c (register pokes for ACON1/ACON2/MC1/TSL/GPIO).

#include "hcpp/types.hpp"
#include "kernel/chips/aw2-saa7146.h"
#include "kernel/chips/aw2-table.h"

namespace hype::chips::aw2 {

    // ---- Init sequence ---------------------------------------------------------
    // Each entry is a (register offset, value) write in probe order.
    // Values derived from Linux aw2-saa7146.c init path: audio clock
    // routed from PLL, TSL in 8-slot mode, A1/A2 DMA engines enabled,
    // DDR (bit-clock doubling) off for 44.1 kHz I2S.
    struct Aw2InitPair {
        hcpp::u32 reg;
        hcpp::u32 value;
    };

    inline constexpr Aw2InitPair aw2_init_seq[] = {
        // 1. Bus mastering + burst config.
        {saa7146::reg_bctr, 0x00E00001},
        {saa7146::reg_btct, 0x00000101},
        // 2. Audio config: I2S, MSB-first, BCLK non-inverted.
        {saa7146::reg_acon1, saa7146::acon1_lfs1 | 0x00000010},
        {saa7146::reg_acon2, saa7146::acon2_tsl_rdy},
        // 3. Multimedia control: enable audio engines 1+2.
        {saa7146::reg_mc1, saa7146::mc1_3v | saa7146::mc1_taps |
             saa7146::mc1_a1_sink | saa7146::mc1_a2_sink},
        {saa7146::reg_mc2, saa7146::mc2_upld1 | saa7146::mc2_upld2},
        // 4. GPIO: all codec-attached pins as outputs, low.
        {saa7146::reg_gpio_ctrl, 0x00000000},
        // 5. PCI address modifiers: linear 32-bit DMA.
        {saa7146::reg_pci_adp1, 0x00000000},
        {saa7146::reg_pci_adp2, 0x00000000},
        {saa7146::reg_ddp, 0x00000000},
        // 6. Interrupt mask: audio DMA + RPS only.
        {saa7146::reg_ier, saa7146::irq_a1_in | saa7146::irq_a1_out |
             saa7146::irq_a2_in | saa7146::irq_a2_out |
             saa7146::irq_rps0 | saa7146::irq_rps1},
    };
    constexpr hcpp::size_t aw2_init_seq_count =
        sizeof(aw2_init_seq) / sizeof(aw2_init_seq[0]);

    // ---- Runtime chip state --------------------------------------------------
    struct Aw2Core {
        hcpp::uintptr_t mmio;         // SAA7146 BAR0
        const Aw2Card *card;          // board row
        hcpp::u16 vendor;
        hcpp::u16 device;
        hcpp::u8 irq;
        bool powered;
        // DMA state, one slot per direction per stream.
        hcpp::u32 dma_base[dma_channels][2];
        hcpp::u32 dma_prot[dma_channels][2];
        // Current TSL shadow (last written 8 slot words).
        hcpp::u32 tsl1_shadow[16];
        hcpp::u32 tsl2_shadow[16];
    };

    // Codec power sequencing on the GPIO pin: assert, wait, release.
    constexpr hcpp::u8 gpio_codec_reset = 0x01;
    constexpr hcpp::u8 gpio_codec_power = 0x02;
    constexpr hcpp::u8 gpio_codec_mute = 0x04;
    constexpr hcpp::u32 codec_reset_delay_us = 100;

    // Pin helpers — GPIO_CTRL bit map is board-specific but the
    // set/clr pattern is uniform.
    inline void gpio_set(hcpp::uintptr_t mmio, hcpp::u32 mask) noexcept {
        saa7146::write(mmio, saa7146::reg_gpio_ctrl,
                       saa7146::read(mmio, saa7146::reg_gpio_ctrl) |
                           mask);
    }
    inline void gpio_clr(hcpp::uintptr_t mmio, hcpp::u32 mask) noexcept {
        saa7146::write(mmio, saa7146::reg_gpio_ctrl,
                       saa7146::read(mmio, saa7146::reg_gpio_ctrl) &
                           ~mask);
    }

}  // namespace hype::chips::aw2
