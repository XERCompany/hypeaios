#pragma once

// Aureal AU8820 "Vortex" — per-model hardware register map.
// Original Vortex: 16 ADB engines, 32-voice wavetable (2 banks of
// 0x20), 16 mixer inputs, no A3D, no EQ.
//
// Reference: Linux sound/pci/au88x0/au8820.h (IO addresses recovered
// from Aureal's asp4core.vxd, functions 0x5D5A0/0x80674/0x80AFF).

#include "hcpp/types.hpp"

namespace hype::chips::au88x0::m8820 {

    constexpr const char *card_name = "Aureal Vortex";
    constexpr const char *card_name_short = "au8820";

    constexpr hcpp::u8 nr_adb = 0x10;
    constexpr hcpp::u8 nr_wt = 0x20;    // 32 wavetable voices
    constexpr hcpp::u8 nr_src = 0x10;
    constexpr hcpp::u8 nr_a3d = 0x00;   // no A3D
    constexpr hcpp::u8 nr_mixin = 0x10;
    constexpr hcpp::u8 nr_mixout = 0x10;

    // ---- ADB DMA ------------------------------------------------------
    constexpr hcpp::u32 vortex_adbdma_stat = 0x105C0;
    constexpr hcpp::u32 pos_mask = 0x00000FFF;
    constexpr hcpp::u8 pos_shift = 0x0;
    constexpr hcpp::u32 adb_subbuf_mask = 0x00003000;
    constexpr hcpp::u8 adb_subbuf_shift = 0xC;
    constexpr hcpp::u32 vortex_adbdma_ctrl = 0x10580;
    constexpr hcpp::u32 offset_mask = 0x00000FFF;
    constexpr hcpp::u8 offset_shift = 0x0;
    constexpr hcpp::u32 ie_mask = 0x00001000;
    constexpr hcpp::u8 ie_shift = 0xC;
    constexpr hcpp::u32 dir_mask = 0x00002000;
    constexpr hcpp::u8 dir_shift = 0xD;
    constexpr hcpp::u32 fmt_mask = 0x0003C000;
    constexpr hcpp::u8 fmt_shift = 0xE;
    constexpr hcpp::u32 vortex_adbdma_bufcfg0 = 0x10400;
    constexpr hcpp::u32 vortex_adbdma_bufcfg1 = 0x10404;
    constexpr hcpp::u32 vortex_adbdma_bufbase = 0x10200;
    constexpr hcpp::u32 vortex_adbdma_start = 0x106C0;
    constexpr hcpp::u32 vortex_adbdma_status = 0x10600;

    // ---- ADB route tables ----------------------------------------------
    constexpr hcpp::u32 vortex_adb_sr = 0x10A00;
    constexpr hcpp::u32 vortex_adb_rtbase = 0x10800;
    constexpr hcpp::u16 vortex_adb_rtbase_count = 103;
    constexpr hcpp::u32 vortex_adb_chnbase = 0x1099C;
    constexpr hcpp::u8 vortex_adb_chnbase_count = 22;
    constexpr hcpp::u16 route_mask = 0x3FFF;
    constexpr hcpp::u16 adb_mask = 0x7F;
    constexpr hcpp::u8 adb_shift = 0x7;

    // ADB node offsets (7-bit address space on this silicon).
    constexpr hcpp::u8 offset_adbdma = 0x00;
    constexpr hcpp::u8 offset_srcout = 0x10;   // on channel 0x11
    constexpr hcpp::u8 offset_srcin = 0x10;    // on channels < 0x11
    constexpr hcpp::u8 offset_mixout = 0x20;
    constexpr hcpp::u8 offset_mixin = 0x30;
    constexpr hcpp::u8 offset_codecin = 0x48;
    constexpr hcpp::u8 offset_codecout = 0x58;
    constexpr hcpp::u8 offset_sportout = 0x60;
    constexpr hcpp::u8 offset_sportin = 0x50;
    constexpr hcpp::u8 offset_efxout = 0x50;
    constexpr hcpp::u8 offset_efxin = 0x40;
    constexpr hcpp::u8 offset_a3dout = 0x00;   // no HRTF on this card
    constexpr hcpp::u8 offset_a3din = 0x00;
    constexpr hcpp::u8 offset_wtout = 0x58;

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
    inline hcpp::u8 adb_sportout(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_sportout);
    }
    inline hcpp::u8 adb_sportin(hcpp::u8 x) noexcept {
        return static_cast<hcpp::u8>(x + offset_sportin);
    }
    inline hcpp::u8 adb_wtout(hcpp::u8 y) noexcept {
        return static_cast<hcpp::u8>(y + offset_wtout);
    }

    // ---- WTDMA ----------------------------------------------------------
    constexpr hcpp::u32 vortex_wtdma_ctrl = 0x10500;
    constexpr hcpp::u32 vortex_wtdma_stat = 0x10500;
    constexpr hcpp::u8 wt_subbuf_shift = 0x15;
    constexpr hcpp::u32 wt_subbuf_mask = 0x3u << wt_subbuf_shift;
    constexpr hcpp::u32 vortex_wtdma_bufbase = 0x10000;
    constexpr hcpp::u32 vortex_wtdma_bufcfg0 = 0x10300;
    constexpr hcpp::u32 vortex_wtdma_bufcfg1 = 0x10304;
    constexpr hcpp::u32 vortex_wtdma_start = 0x10640;
    constexpr hcpp::u32 vortex_wt_base = 0x9000;

    // ---- Mixer ----------------------------------------------------------
    constexpr hcpp::u32 vortex_mixer_sr = 0x9F00;
    constexpr hcpp::u32 vortex_mixer_clip = 0x9F80;
    constexpr hcpp::u32 vortex_mixer_chnbase = 0x9E40;
    constexpr hcpp::u32 vortex_mixer_rtbase = 0x9E00;
    constexpr hcpp::u8 mixer_rtbase_size = 0x26;
    constexpr hcpp::u32 vortex_mix_enin = 0x9A00;
    constexpr hcpp::u32 vortex_mix_smp = 0x9C00;
    constexpr hcpp::u32 vortex_mix_invol_a = 0x9000;
    constexpr hcpp::u32 vortex_mix_invol_b = 0x8000;
    constexpr hcpp::u32 vortex_mix_vol_a = 0x9800;
    constexpr hcpp::u32 vortex_mix_vol_b = 0x8800;
    constexpr hcpp::u8 vol_min = 0x80;
    constexpr hcpp::u8 vol_max = 0x7F;
    constexpr hcpp::u8 mix_defigain = 0x08;
    constexpr hcpp::u8 mix_defogain = 0x08;

    // ---- SRC -------------------------------------------------------------
    constexpr hcpp::u32 vortex_srcblock_sr = 0xCCC0;
    constexpr hcpp::u32 vortex_src_chnbase = 0xCC40;
    constexpr hcpp::u32 vortex_src_rtbase = 0xCC00;
    constexpr hcpp::u32 vortex_src_source = 0xCCC4;
    constexpr hcpp::u32 vortex_src_sourcesize = 0xCCC8;
    constexpr hcpp::u32 vortex_src_u0 = 0xCE00;
    constexpr hcpp::u32 vortex_src_drift0 = 0xCE80;
    constexpr hcpp::u32 vortex_src_drift1 = 0xCEC0;
    constexpr hcpp::u32 vortex_src_u1 = 0xCF00;
    constexpr hcpp::u32 vortex_src_drift2 = 0xCF40;
    constexpr hcpp::u32 vortex_src_u2 = 0xCF80;
    constexpr hcpp::u32 vortex_src_data = 0xC800;
    constexpr hcpp::u32 vortex_src_data0 = 0xC000;
    constexpr hcpp::u32 vortex_src_convratio = 0xCE40;

    // ---- FIFO --------------------------------------------------------------
    constexpr hcpp::u32 vortex_fifo_adbctrl = 0xF800;
    constexpr hcpp::u32 vortex_fifo_wtctrl = 0xF840;
    constexpr hcpp::u32 fifo_rdonly = 0x00000001;
    constexpr hcpp::u32 fifo_ctrl = 0x00000002;
    constexpr hcpp::u32 fifo_valid = 0x00000010;
    constexpr hcpp::u32 fifo_empty = 0x00000020;
    constexpr hcpp::u32 fifo_u0 = 0x00001000;
    constexpr hcpp::u32 fifo_u1 = 0x00010000;
    constexpr hcpp::u8 fifo_size_bits = 5;
    constexpr hcpp::u32 fifo_size = 1u << fifo_size_bits;
    constexpr hcpp::u32 fifo_mask = fifo_size - 1;
    constexpr hcpp::u32 vortex_fifo_adbdata = 0xE000;
    constexpr hcpp::u32 vortex_fifo_wtdata = 0xE800;

    // ---- Codec/SPDIF --------------------------------------------------------
    constexpr hcpp::u32 vortex_codec_ctrl = 0x11984;
    constexpr hcpp::u32 vortex_codec_en = 0x11990;
    constexpr hcpp::u32 en_codec = 0x00000300;
    constexpr hcpp::u32 en_sport = 0x00030000;
    constexpr hcpp::u32 en_spdif = 0x000C0000;
    constexpr hcpp::u32 vortex_codec_chn = 0x11880;
    constexpr hcpp::u32 vortex_codec_io = 0x11988;

    constexpr hcpp::u32 vortex_spdif_flags = 0x1005C;
    constexpr hcpp::u32 vortex_spdif_cfg0 = 0x119D0;
    constexpr hcpp::u32 vortex_spdif_cfg1 = 0x119D4;
    constexpr hcpp::u32 vortex_spdif_smprate = 0x11994;
    constexpr hcpp::u32 vortex_smp_time = 0x11998;

    // ---- IRQ/CTRL --------------------------------------------------------------
    constexpr hcpp::u32 vortex_irq_source = 0x12800;
    constexpr hcpp::u32 vortex_irq_ctrl = 0x12804;
    constexpr hcpp::u32 vortex_stat = 0x12808;
    constexpr hcpp::u32 vortex_ctrl = 0x1280C;
    constexpr hcpp::u32 ctrl_midi_en = 0x00000001;
    constexpr hcpp::u32 ctrl_midi_port = 0x00000060;
    constexpr hcpp::u32 ctrl_game_en = 0x00000008;
    constexpr hcpp::u32 ctrl_game_port = 0x00000E00;
    constexpr hcpp::u32 ctrl_irq_enable = 0x4000;
    constexpr hcpp::u32 vortex_irq_stat = 0x1199C;

    constexpr hcpp::u32 vortex_dma_buffer = 0x10200;
    constexpr hcpp::u32 vortex_engine_ctrl = 0x1060C;
    constexpr hcpp::u32 engine_init = 0x0;

    // ---- MIDI / gameport ----------------------------------------------------------
    constexpr hcpp::u32 vortex_midi_data = 0x11000;
    constexpr hcpp::u32 vortex_midi_cmd = 0x11004;
    constexpr hcpp::u32 vortex_game_legacy = 0x11008;
    constexpr hcpp::u32 vortex_ctrl2 = 0x1100C;
    constexpr hcpp::u32 ctrl2_game_adcmode = 0x40;
    constexpr hcpp::u32 vortex_game_axis = 0x11010;
    constexpr hcpp::u8 axis_size = 4;
    constexpr hcpp::u16 axis_range = 0x1FFF;

}  // namespace hype::chips::au88x0::m8820
