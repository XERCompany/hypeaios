#pragma once

// Terratec AW2 DMA engine layer — per-stream programming of the
// SAA7146 audio DMA pointers (BASE/PROT pairs), the TSL slot map
// used to route codec lanes onto the audio serial ports, and the
// hardware-parameter constraints ALSA exposes (S16_LE @ 44.1 kHz,
// ≤32 KiB buffers).
//
// Reference: Linux sound/pci/aw2/aw2-alsa.c pcm ops + aw2-saa7146.c
// DMA start/stop helpers.

#include "hcpp/types.hpp"
#include "kernel/chips/aw2-saa7146.h"
#include "kernel/chips/aw2-table.h"

namespace hype::chips::aw2 {

    // ---- DMA register selection -----------------------------------------------
    // channel: 0 = A1 (analog), 1 = A2 (digital). dir: 0 = in, 1 = out.
    inline hcpp::u32 dma_base_reg(hcpp::u8 ch, hcpp::u8 dir) noexcept {
        return (ch == 0)
            ? (dir ? saa7146::reg_base_a1_out : saa7146::reg_base_a1_in)
            : (dir ? saa7146::reg_base_a2_out : saa7146::reg_base_a2_in);
    }
    inline hcpp::u32 dma_prot_reg(hcpp::u8 ch, hcpp::u8 dir) noexcept {
        return (ch == 0)
            ? (dir ? saa7146::reg_prot_a1_out : saa7146::reg_prot_a1_in)
            : (dir ? saa7146::reg_base_a2_outp : saa7146::reg_prot_a2_in);
    }

    // Interrupt flag for a channel/direction.
    inline hcpp::u32 dma_irq_bit(hcpp::u8 ch, hcpp::u8 dir) noexcept {
        return (ch == 0)
            ? (dir ? saa7146::irq_a1_out : saa7146::irq_a1_in)
            : (dir ? saa7146::irq_a2_out : saa7146::irq_a2_in);
    }

    // ---- Stream state ----------------------------------------------------------
    struct Aw2Stream {
        hcpp::u8 channel;          // 0 analog / 1 digital
        hcpp::u8 dir;              // 0 capture / 1 playback
        hcpp::u8 channels;         // interleaved sample count
        hcpp::u32 dma_addr;        // physical DMA base
        hcpp::u32 dma_size;        // bytes
        hcpp::u32 period_size;
        hcpp::u32 periods;
        hcpp::u32 pos;             // last IRQ position
        bool running;
    };

    // ---- Constraints -----------------------------------------------------------
    // ALSA snd_pcm_hardware for AW2: S16_LE only, 2-8 interleaved
    // channels, period >= 64 bytes, buffer <= 32768.
    constexpr hcpp::u8 channels_min = 2;
    constexpr hcpp::u8 channels_max = 8;
    constexpr hcpp::u32 period_bytes_min = 64;
    constexpr hcpp::u32 period_bytes_max = 16384;
    constexpr hcpp::u32 periods_min = 2;
    constexpr hcpp::u32 periods_max = 32;

    // Sanity check a requested hardware config.
    inline bool hwparams_ok(hcpp::u8 channels, hcpp::u32 buffer_bytes,
                            hcpp::u32 period_bytes) noexcept {
        return channels >= channels_min && channels <= channels_max &&
               buffer_bytes <= buffer_max &&
               period_bytes >= period_bytes_min &&
               period_bytes <= period_bytes_max &&
               period_bytes % (channels * frame_bytes) == 0;
    }

    // ---- Register programming ------------------------------------------------
    // Program one DMA channel's base/protect registers.
    inline void dma_program(hcpp::uintptr_t mmio, hcpp::u8 ch,
                            hcpp::u8 dir, hcpp::u32 addr,
                            hcpp::u32 size) noexcept {
        saa7146::write(mmio, dma_base_reg(ch, dir), addr);
        saa7146::write(mmio, dma_prot_reg(ch, dir), addr + size);
    }

    // Build the TSL words for I2S 8-slot mode: linear slot order
    // 0..7, packed 4 slots per word (Linux aw2-tsl.c default table).
    inline void tsl_fill_i2s(hcpp::u32 *tsl /*>=2 words*/) noexcept {
        tsl[0] = tsl_pack(0, 1, 2, 3);
        tsl[1] = tsl_pack(4, 5, 6, 7);
    }

    // Current playback position: read DMA base mirror — SAA7146
    // updates the PROT register to the current write pointer.
    inline hcpp::u32 dma_position(hcpp::uintptr_t mmio, hcpp::u8 ch,
                                  hcpp::u8 dir) noexcept {
        return saa7146::read(mmio, dma_prot_reg(ch, dir));
    }

}  // namespace hype::chips::aw2
