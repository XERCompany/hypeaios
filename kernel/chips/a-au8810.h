#pragma once

// Aureal AU8810 "Advantage" — per-model hardware register map.
// The AU8810 is the cost-reduced Vortex: ADB engine but no wavetable
// (NR_WT=0), 16 ADB DMA engines, 32 mixer inputs, A3D present.
//
// Reference: Linux sound/pci/au88x0/au8810.h (the register values were
// recovered from Aureal's asp4core.vxd by the OpenVortex project).

#include "hcpp/types.hpp"

namespace hype::chips::au88x0::m8810 {

    constexpr const char *card_name = "Aureal Advantage";
    constexpr const char *card_name_short = "au8810";

    // Block sizes — what this silicon actually implements.
    constexpr hcpp::u8 nr_adb = 0x10;    // ADB DMA engines
    constexpr hcpp::u8 nr_wt = 0x00;     // no wavetable engine
    constexpr hcpp::u8 nr_src = 0x10;    // sample-rate converters
    constexpr hcpp::u8 nr_a3d = 0x10;    // A3D slices
    constexpr hcpp::u8 nr_mixin = 0x20;  // mixer inputs
    constexpr hcpp::u8 nr_mixout = 0x10; // mixer outputs

    // ---- ADB DMA engine ---------------------------------------------
    constexpr hcpp::u32 vortex_adbdma_stat = 0x27E00;  // RO pos/subbuf
    constexpr hcpp::u32 pos_mask = 0x00000FFF;
    constexpr hcpp::u8 pos_shift = 0x0;
    constexpr hcpp::u32 adb_subbuf_mask = 0x00003000;
    constexpr hcpp::u8 adb_subbuf_shift = 0xC;
    constexpr hcpp::u32 vortex_adbdma_ctrl = 0x27180;  // WO fmt/flags/pos
    constexpr hcpp::u32 offset_mask = 0x00000FFF;
    constexpr hcpp::u8 offset_shift = 0x0;
    constexpr hcpp::u32 ie_mask = 0x00001000;
    constexpr hcpp::u8 ie_shift = 0xC;
    constexpr hcpp::u32 dir_mask = 0x00002000;
    constexpr hcpp::u8 dir_shift = 0xD;
    constexpr hcpp::u32 fmt_mask = 0x0003C000;
    constexpr hcpp::u8 fmt_shift = 0xE;
    constexpr hcpp::u32 vortex_adbdma_bufcfg0 = 0x27100;
    constexpr hcpp::u32 vortex_adbdma_bufcfg1 = 0x27104;
    constexpr hcpp::u32 vortex_adbdma_bufbase = 0x27000;
    constexpr hcpp::u32 vortex_adbdma_start = 0x27C00;
    constexpr hcpp::u32 vortex_adbdma_status = 0x27A90;

    // ---- WTDMA (present but WT voice count is zero) ------------------
    constexpr hcpp::u32 vortex_wtdma_ctrl = 0x27FD8;
    constexpr hcpp::u32 vortex_wtdma_stat = 0x27FE8;
    constexpr hcpp::u32 wt_subbuf_mask = 0x3;
    constexpr hcpp::u8 wt_subbuf_shift = 0xC;
    constexpr hcpp::u32 vortex_wtdma_bufbase = 0x27FC0;
    constexpr hcpp::u32 vortex_wtdma_bufcfg0 = 0x27FD0;
    constexpr hcpp::u32 vortex_wtdma_bufcfg1 = 0x27FD4;
    constexpr hcpp::u32 vortex_wtdma_start = 0x27FE4;

    // ---- ADB route tables --------------------------------------------
    constexpr hcpp::u32 vortex_adb_sr = 0x28400;      // per-channel rate
    constexpr hcpp::u32 vortex_adb_rtbase = 0x28000;
    constexpr hcpp::u16 vortex_adb_rtbase_count = 173;
    constexpr hcpp::u32 vortex_adb_chnbase = 0x282B4;
    constexpr hcpp::u8 vortex_adb_chnbase_count = 24;
    constexpr hcpp::u16 route_mask = 0xFFFF;
    constexpr hcpp::u16 source_mask = 0xFF00;
    constexpr hcpp::u16 adb_mask = 0xFF;
    constexpr hcpp::u8 adb_shift = 0x8;

    // ADB node address offsets (source/sink bases).
    constexpr hcpp::u8 offset_adbdma = 0x00;
    constexpr hcpp::u8 offset_srcin = 0x40;
    constexpr hcpp::u8 offset_srcout = 0x20;
    constexpr hcpp::u8 offset_mixin = 0x50;
    constexpr hcpp::u8 offset_mixout = 0x30;
    constexpr hcpp::u8 offset_codecin = 0x70;
    constexpr hcpp::u8 offset_codecout = 0x88;
    constexpr hcpp::u8 offset_sportin = 0x78;    // ch 0x13
    constexpr hcpp::u8 offset_sportout = 0x90;
    constexpr hcpp::u8 offset_spdifout = 0x92;   // ch 0x14
    constexpr hcpp::u8 offset_eqin = 0xA0;
    constexpr hcpp::u8 offset_eqout = 0x7E;      // 2 routes on ch 0x11
    constexpr hcpp::u8 offset_xtalkout = 0x66;   // xtalk canceller src
    constexpr hcpp::u8 offset_xtalkin = 0x96;    // xtalk canceller sink
    constexpr hcpp::u8 offset_a3din = 0x70;      // ADB sink
    constexpr hcpp::u8 offset_a3dout = 0xA6;     // ADB src, 2 routes/slice
    constexpr hcpp::u8 offset_efxin = 0x80;      // ADB sink
    constexpr hcpp::u8 offset_efxout = 0x68;     // ADB source

    // Route-translation helpers (Linux ADB_*()).
    inline hcpp::u8 adb_dma(hcpp::u8 x) noexcept { return x; }
    inline hcpp::u8 adb_srcout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_srcout);
    }
    inline hcpp::u8 adb_srcin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_srcin);
    }
    inline hcpp::u8 adb_mixout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_mixout);
    }
    inline hcpp::u8 adb_mixin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_mixin);
    }
    inline hcpp::u8 adb_codecin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_codecin);
    }
    inline hcpp::u8 adb_codecout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_codecout);
    }
    inline hcpp::u8 adb_sportin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_sportin);
    }
    inline hcpp::u8 adb_sportout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_sportout);
    }
    inline hcpp::u8 adb_spdifout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_spdifout);
    }
    inline hcpp::u8 adb_eqin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_eqin);
    }
    inline hcpp::u8 adb_eqout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_eqout);
    }
    inline hcpp::u8 adb_a3dout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_a3dout);
    }
    inline hcpp::u8 adb_a3din(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_a3din);
    }
    inline hcpp::u8 adb_xtalkin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_xtalkin);
    }
    inline hcpp::u8 adb_xtalkout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_xtalkout);
    }

    // Canonical mixer endpoints.
    constexpr hcpp::u8 mix_outl = 0x0E;
    constexpr hcpp::u8 mix_outr = 0x0F;
    constexpr hcpp::u8 mix_inl = 0x1E;
    constexpr hcpp::u8 mix_inr = 0x1F;
    constexpr hcpp::u8 mix_defigain = 0x08;   // 0x08 => +6 dB
    constexpr hcpp::u8 mix_defogain = 0x08;

    // ---- Mixer ------------------------------------------------------
    constexpr hcpp::u32 vortex_mixer_sr = 0x21F00;
    constexpr hcpp::u32 vortex_mixer_clip = 0x21F80;
    constexpr hcpp::u32 vortex_mixer_chnbase = 0x21E40;
    constexpr hcpp::u32 vortex_mixer_rtbase = 0x21E00;
    constexpr hcpp::u8 mixer_rtbase_size = 0x38;
    constexpr hcpp::u32 vortex_mix_enin = 0x21A00;
    constexpr hcpp::u32 vortex_mix_smp = 0x21C00;

    constexpr hcpp::u32 vortex_mix_invol_a = 0x21000;  // input target
    constexpr hcpp::u32 vortex_mix_invol_b = 0x20000;  // input current
    constexpr hcpp::u32 vortex_mix_vol_a = 0x21800;    // output target
    constexpr hcpp::u32 vortex_mix_vol_b = 0x20800;    // output current

    constexpr hcpp::u8 vol_min = 0x80;   // muted input level
    constexpr hcpp::u8 vol_max = 0x7F;

    // ---- SRC block --------------------------------------------------
    constexpr hcpp::u32 vortex_src_chnbase = 0x26C40;
    constexpr hcpp::u32 vortex_src_rtbase = 0x26C00;
    constexpr hcpp::u32 vortex_srcblock_sr = 0x26CC0;
    constexpr hcpp::u32 vortex_src_source = 0x26CC4;
    constexpr hcpp::u32 vortex_src_sourcesize = 0x26CC8;
    constexpr hcpp::u32 vortex_src_convratio = 0x26E40;
    constexpr hcpp::u32 vortex_src_drift0 = 0x26E80;
    constexpr hcpp::u32 vortex_src_drift1 = 0x26EC0;
    constexpr hcpp::u32 vortex_src_drift2 = 0x26F40;
    constexpr hcpp::u32 vortex_src_u0 = 0x26E00;
    constexpr hcpp::u32 u0_slowlock = 0x200;
    constexpr hcpp::u32 vortex_src_u1 = 0x26F00;
    constexpr hcpp::u32 vortex_src_u2 = 0x26F80;
    constexpr hcpp::u32 vortex_src_data = 0x26800;
    constexpr hcpp::u32 vortex_src_data0 = 0x26000;

    // ---- FIFO --------------------------------------------------------
    constexpr hcpp::u32 vortex_fifo_adbctrl = 0x16100;
    constexpr hcpp::u32 vortex_fifo_wtctrl = 0x16000;
    constexpr hcpp::u32 fifo_rdonly = 0x00000001;
    constexpr hcpp::u32 fifo_ctrl = 0x00000002;
    constexpr hcpp::u32 fifo_valid = 0x00000010;
    constexpr hcpp::u32 fifo_empty = 0x00000020;
    constexpr hcpp::u32 fifo_u0 = 0x00001000;
    constexpr hcpp::u32 fifo_u1 = 0x00010000;
    constexpr hcpp::u8 fifo_size_bits = 5;
    constexpr hcpp::u32 fifo_size = 1u << fifo_size_bits;  // 0x20
    constexpr hcpp::u32 fifo_mask = fifo_size - 1;
    constexpr hcpp::u32 fifo_bits = 0x03880000;
    constexpr hcpp::u32 vortex_fifo_adbdata = 0x14000;
    constexpr hcpp::u32 vortex_fifo_wtdata = 0x10000;

    // ---- Codec/SPDIF ---------------------------------------------------
    constexpr hcpp::u32 vortex_codec_ctrl = 0x29184;
    constexpr hcpp::u32 vortex_codec_en = 0x29190;
    constexpr hcpp::u32 en_codec0 = 0x00000300;
    constexpr hcpp::u32 en_ac98 = 0x00000C00;   // modem AC'98 slots
    constexpr hcpp::u32 en_codec1 = 0x00003000;
    constexpr hcpp::u32 en_codec = en_codec0 | en_codec1;
    constexpr hcpp::u32 en_sport = 0x00030000;
    constexpr hcpp::u32 en_spdif = 0x000C0000;
    constexpr hcpp::u32 vortex_codec_chn = 0x29080;
    constexpr hcpp::u32 vortex_codec_io = 0x29188;

    constexpr hcpp::u32 vortex_spdif_flags = 0x2205C;
    constexpr hcpp::u32 vortex_spdif_cfg0 = 0x291D0;
    constexpr hcpp::u32 vortex_spdif_cfg1 = 0x291D4;
    constexpr hcpp::u32 vortex_spdif_smprate = 0x29194;
    constexpr hcpp::u32 vortex_smp_time = 0x29198;
    constexpr hcpp::u32 vortex_modem_ctrl = 0x291AC;

    // ---- IRQ/CTRL ------------------------------------------------------
    constexpr hcpp::u32 vortex_irq_source = 0x2A000;
    constexpr hcpp::u32 vortex_irq_ctrl = 0x2A004;
    constexpr hcpp::u32 vortex_stat = 0x2A008;
    constexpr hcpp::u32 vortex_ctrl = 0x2A00C;
    constexpr hcpp::u32 ctrl_midi_en = 0x00000001;
    constexpr hcpp::u32 ctrl_midi_port = 0x00000060;
    constexpr hcpp::u32 ctrl_game_en = 0x00000008;
    constexpr hcpp::u32 ctrl_game_port = 0x00000E00;
    constexpr hcpp::u32 ctrl_irq_enable = 0x00004000;
    constexpr hcpp::u32 vortex_irq_stat = 0x2919C;   // timer period/ack

    constexpr hcpp::u32 vortex_engine_ctrl = 0x27AE8;
    constexpr hcpp::u32 engine_init = 0x1380000;

    // ---- MIDI / gameport -------------------------------------------------
    constexpr hcpp::u32 vortex_midi_data = 0x28800;
    constexpr hcpp::u32 vortex_midi_cmd = 0x28804;
    constexpr hcpp::u32 vortex_ctrl2 = 0x2880C;
    constexpr hcpp::u32 ctrl2_game_adcmode = 0x40;
    constexpr hcpp::u32 vortex_game_legacy = 0x28808;
    constexpr hcpp::u32 vortex_game_axis = 0x28810;
    constexpr hcpp::u8 axis_size = 4;
    constexpr hcpp::u16 axis_range = 0x1FFF;

}  // namespace hype::chips::au88x0::m8810
