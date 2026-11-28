#pragma once

// Aureal Vortex core definitions shared by all AU88x0 models: IRQ
// flags, ADB resource types, codec I/O bitfield encoding, FIFO states,
// stream/card state structs and the MIDI/uart constants.
//
// Reference: Linux sound/pci/au88x0/au88x0.h.

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0pci.h"

namespace hype::chips::au88x0 {

    // ---- IRQ source flags (VORTEX_IRQ_SOURCE) ---------------------------
    constexpr hcpp::u32 irq_err_mask = 0x00FF;
    constexpr hcpp::u32 irq_fatal = 0x0001;
    constexpr hcpp::u32 irq_parity = 0x0002;
    constexpr hcpp::u32 irq_reg = 0x0004;
    constexpr hcpp::u32 irq_fifo = 0x0008;
    constexpr hcpp::u32 irq_dma = 0x0010;
    constexpr hcpp::u32 irq_pcmout = 0x0020;   // PCM out page crossing
    constexpr hcpp::u32 irq_timer = 0x1000;
    constexpr hcpp::u32 irq_midi = 0x2000;
    constexpr hcpp::u32 irq_modem = 0x4000;

    // ---- ADB resource kinds (vortex_adb_checkinout) ----------------------
    constexpr hcpp::u32 res_dma = 0x00000000;
    constexpr hcpp::u32 res_src = 0x00000001;
    constexpr hcpp::u32 res_mixin = 0x00000002;
    constexpr hcpp::u32 res_mixout = 0x00000003;
    constexpr hcpp::u32 res_a3d = 0x00000004;
    constexpr hcpp::u32 res_last = 0x00000005;

    // ---- Codec I/O word (VORTEX_CODEC_IO) ---------------------------------
    // [31:24] codec index, [23] write flag, [22:16] register address,
    // [15:0] data.
    constexpr hcpp::u8 codec_id_shift = 24;
    constexpr hcpp::u32 codec_write_flag = 0x00800000;
    constexpr hcpp::u8 codec_addshift = 16;
    constexpr hcpp::u32 codec_addmask = 0x7F0000;
    constexpr hcpp::u8 codec_datshift = 0;
    constexpr hcpp::u32 codec_datmask = 0xFFFF;
    // Codec busy/read in progress status bit (CODEC_CTRL).
    constexpr hcpp::u32 codec_busy = 0x0100;

    inline hcpp::u32 codec_io_pack(hcpp::u8 codec, hcpp::u8 reg,
                                   hcpp::u16 data, bool write) noexcept {
        return (static_cast<hcpp::u32>(codec) << codec_id_shift) |
               (write ? codec_write_flag : 0) |
               ((static_cast<hcpp::u32>(reg) << codec_addshift) &
                codec_addmask) |
               ((static_cast<hcpp::u32>(data) << codec_datshift) &
                codec_datmask);
    }

    // ---- FIFO software states -----------------------------------------------
    constexpr hcpp::u8 fifo_stop = 0;
    constexpr hcpp::u8 fifo_start = 1;
    constexpr hcpp::u8 fifo_pause = 2;

    // ---- PCM device indices -----------------------------------------------------
    constexpr hcpp::u8 pcm_adb = 0;
    constexpr hcpp::u8 pcm_spdif = 1;
    constexpr hcpp::u8 pcm_a3d = 2;
    constexpr hcpp::u8 pcm_wt = 3;
    constexpr hcpp::u8 pcm_i2s = 4;
    constexpr hcpp::u8 pcm_last = 5;
    constexpr hcpp::u8 nr_pcm = 0x10;
    constexpr hcpp::u8 nr_wtpb = 0x20;    // WT voices per bank

    // ---- MIDI / MPU-401 -------------------------------------------------------------
    constexpr hcpp::u8 midi_clock_div = 0x61;   // Vortex divider
    constexpr hcpp::u8 mpu401_reset = 0xFF;
    constexpr hcpp::u8 mpu401_enter_uart = 0x3F;
    constexpr hcpp::u8 mpu401_ack = 0xFE;

    // SRC ratio: conversion ratio written to VORTEX_SRC_CONVRATIO.
    // (((rate << 15) / 48000) + 1) / 2 for playback direction.
    inline hcpp::u32 src_ratio(hcpp::u32 x, hcpp::u32 y) noexcept {
        return (((x << 15) / y) + 1) / 2;
    }

    // ---- Stream / card state ------------------------------------------------------
    struct Au88x0Stream {
        hcpp::i32 fifo_enabled;
        hcpp::i32 fifo_status;
        hcpp::u32 dma_ctrl;
        hcpp::i32 dma_unknown;
        hcpp::i32 cfg0;
        hcpp::i32 cfg1;
        hcpp::i32 nr_ch;        // PCM channels in use
        hcpp::i32 type;         // ac97/a3d/spdif/i2s/dsp
        hcpp::i32 dma;          // hardware DMA index
        hcpp::i32 dir;          // direction
        hcpp::u32 resources[5];
        hcpp::i32 nr_periods;
        hcpp::i32 period_bytes;
        hcpp::i32 period_real;
        hcpp::i32 period_virt;
    };

    struct Au88x0PcmVol {
        hcpp::i32 active;
        hcpp::i32 dma;
        hcpp::i32 mixin[4];
        hcpp::i32 vol[4];
    };

    // Top-level device state; per-model register maps live in the
    // a-au88xx.h namespaces and the model row in au88x0pci.h.
    struct Au88x0 {
        hcpp::uintptr_t mmio;
        hcpp::u16 vendor;
        hcpp::u16 device;
        hcpp::u8 rev;
        hcpp::u8 irq;
        hcpp::i32 isquad;           // ext_id SDAC bit cache
        hcpp::i32 spdif_sr;
        hcpp::i32 xt_mode;          // 1=speakers, 0=headphones
        hcpp::i8 mixcapt[2];
        hcpp::i8 mixplayb[4];
        hcpp::i8 mixspdif[2];
        hcpp::i8 mixa3d[2];
        hcpp::i8 mixxtlk[2];
        hcpp::u32 fixed_res[5];
        Au88x0PcmVol pcm_vol[nr_pcm];
        Au88x0Model model;
    };

    // hwread/hwwrite helpers — MMIO is 32-bit register space.
    inline volatile hcpp::u32 *hwreg(hcpp::uintptr_t mmio,
                                     hcpp::u32 offset) noexcept {
        return reinterpret_cast<volatile hcpp::u32 *>(mmio + offset);
    }
    inline hcpp::u32 hwread(hcpp::uintptr_t mmio, hcpp::u32 offset) noexcept {
        return *hwreg(mmio, offset);
    }
    inline void hwwrite(hcpp::uintptr_t mmio, hcpp::u32 offset,
                        hcpp::u32 value) noexcept {
        *hwreg(mmio, offset) = value;
    }

}  // namespace hype::chips::au88x0
