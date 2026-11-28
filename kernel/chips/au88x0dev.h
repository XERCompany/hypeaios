#pragma once

// Aureal Vortex composite device header — pulls together the PCI
// identity, shared core definitions, ADB routing layer, codec I/O,
// EQ, gameport/MIDI and wavetable engine into one include for the
// device-driver half of the chip.
//
// Per-model register maps stay in the a-au88xx.h headers; a driver
// binds exactly one of them at probe time and passes the offsets as
// function parameters (the register addresses differ per silicon
// revision even though semantics are shared).
//
// Reference: Linux sound/pci/au88x0/ (au88x0.c probe + au88x0.h
// device struct).

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0pci.h"
#include "kernel/chips/a-au8810.h"
#include "kernel/chips/a-au8820.h"
#include "kernel/chips/a-au8830.h"
#include "kernel/chips/au88x0core.h"
#include "kernel/chips/au88x0busd.h"
#include "kernel/chips/au88x0codec.h"
#include "kernel/chips/au88x0eq.h"
#include "kernel/chips/au88x0game.h"
#include "kernel/chips/au88x0synth.h"

namespace hype::chips::au88x0 {

    // Register-map vtable: a driver stores the model-selected offsets
    // once instead of branching on model per access. Every field is a
    // MMIO byte offset from the mapped BAR.
    struct Au88x0Regs {
        hcpp::u32 adbdma_stat;
        hcpp::u32 adbdma_ctrl;
        hcpp::u32 adbdma_bufcfg0;
        hcpp::u32 adbdma_bufcfg1;
        hcpp::u32 adbdma_bufbase;
        hcpp::u32 adbdma_start;
        hcpp::u32 adbdma_status;
        hcpp::u32 adb_sr;
        hcpp::u32 adb_rtbase;
        hcpp::u32 adb_chnbase;
        hcpp::u32 mixer_sr;
        hcpp::u32 mixer_chnbase;
        hcpp::u32 mixer_rtbase;
        hcpp::u32 mix_enin;
        hcpp::u32 mix_invol_a;
        hcpp::u32 mix_invol_b;
        hcpp::u32 mix_vol_a;
        hcpp::u32 mix_vol_b;
        hcpp::u32 src_chnbase;
        hcpp::u32 src_rtbase;
        hcpp::u32 src_convratio;
        hcpp::u32 fifo_adbctrl;
        hcpp::u32 fifo_wtctrl;
        hcpp::u32 fifo_adbdata;
        hcpp::u32 fifo_wtdata;
        hcpp::u32 codec_ctrl;
        hcpp::u32 codec_en;
        hcpp::u32 codec_io;
        hcpp::u32 spdif_flags;
        hcpp::u32 irq_source;
        hcpp::u32 irq_ctrl;
        hcpp::u32 irq_stat;
        hcpp::u32 ctrl;
        hcpp::u32 engine_ctrl;
        hcpp::u32 midi_data;
        hcpp::u32 midi_cmd;
        hcpp::u32 game_legacy;
        hcpp::u32 game_axis;
    };

    // Fill the vtable for a given model row.
    inline void regs_bind(Au88x0Regs &r, const Au88x0PciId *id) noexcept {
        if (id == nullptr) return;
        switch (id->model) {
        case Au88x0Model::Au8810:
            r.adbdma_stat = m8810::vortex_adbdma_stat;
            r.adbdma_ctrl = m8810::vortex_adbdma_ctrl;
            r.adbdma_bufcfg0 = m8810::vortex_adbdma_bufcfg0;
            r.adbdma_bufcfg1 = m8810::vortex_adbdma_bufcfg1;
            r.adbdma_bufbase = m8810::vortex_adbdma_bufbase;
            r.adbdma_start = m8810::vortex_adbdma_start;
            r.adbdma_status = m8810::vortex_adbdma_status;
            r.adb_sr = m8810::vortex_adb_sr;
            r.adb_rtbase = m8810::vortex_adb_rtbase;
            r.adb_chnbase = m8810::vortex_adb_chnbase;
            r.mixer_sr = m8810::vortex_mixer_sr;
            r.mixer_chnbase = m8810::vortex_mixer_chnbase;
            r.mixer_rtbase = m8810::vortex_mixer_rtbase;
            r.mix_enin = m8810::vortex_mix_enin;
            r.mix_invol_a = m8810::vortex_mix_invol_a;
            r.mix_invol_b = m8810::vortex_mix_invol_b;
            r.mix_vol_a = m8810::vortex_mix_vol_a;
            r.mix_vol_b = m8810::vortex_mix_vol_b;
            r.src_chnbase = m8810::vortex_src_chnbase;
            r.src_rtbase = m8810::vortex_src_rtbase;
            r.src_convratio = m8810::vortex_src_convratio;
            r.fifo_adbctrl = m8810::vortex_fifo_adbctrl;
            r.fifo_wtctrl = m8810::vortex_fifo_wtctrl;
            r.fifo_adbdata = m8810::vortex_fifo_adbdata;
            r.fifo_wtdata = m8810::vortex_fifo_wtdata;
            r.codec_ctrl = m8810::vortex_codec_ctrl;
            r.codec_en = m8810::vortex_codec_en;
            r.codec_io = m8810::vortex_codec_io;
            r.spdif_flags = m8810::vortex_spdif_flags;
            r.irq_source = m8810::vortex_irq_source;
            r.irq_ctrl = m8810::vortex_irq_ctrl;
            r.irq_stat = m8810::vortex_irq_stat;
            r.ctrl = m8810::vortex_ctrl;
            r.engine_ctrl = m8810::vortex_engine_ctrl;
            r.midi_data = m8810::vortex_midi_data;
            r.midi_cmd = m8810::vortex_midi_cmd;
            r.game_legacy = m8810::vortex_game_legacy;
            r.game_axis = m8810::vortex_game_axis;
            break;
        case Au88x0Model::Au8820:
            r.adbdma_stat = m8820::vortex_adbdma_stat;
            r.adbdma_ctrl = m8820::vortex_adbdma_ctrl;
            r.adbdma_bufcfg0 = m8820::vortex_adbdma_bufcfg0;
            r.adbdma_bufcfg1 = m8820::vortex_adbdma_bufcfg1;
            r.adbdma_bufbase = m8820::vortex_adbdma_bufbase;
            r.adbdma_start = m8820::vortex_adbdma_start;
            r.adbdma_status = m8820::vortex_adbdma_status;
            r.adb_sr = m8820::vortex_adb_sr;
            r.adb_rtbase = m8820::vortex_adb_rtbase;
            r.adb_chnbase = m8820::vortex_adb_chnbase;
            r.mixer_sr = m8820::vortex_mixer_sr;
            r.mixer_chnbase = m8820::vortex_mixer_chnbase;
            r.mixer_rtbase = m8820::vortex_mixer_rtbase;
            r.mix_enin = m8820::vortex_mix_enin;
            r.mix_invol_a = m8820::vortex_mix_invol_a;
            r.mix_invol_b = m8820::vortex_mix_invol_b;
            r.mix_vol_a = m8820::vortex_mix_vol_a;
            r.mix_vol_b = m8820::vortex_mix_vol_b;
            r.src_chnbase = m8820::vortex_src_chnbase;
            r.src_rtbase = m8820::vortex_src_rtbase;
            r.src_convratio = m8820::vortex_src_convratio;
            r.fifo_adbctrl = m8820::vortex_fifo_adbctrl;
            r.fifo_wtctrl = m8820::vortex_fifo_wtctrl;
            r.fifo_adbdata = m8820::vortex_fifo_adbdata;
            r.fifo_wtdata = m8820::vortex_fifo_wtdata;
            r.codec_ctrl = m8820::vortex_codec_ctrl;
            r.codec_en = m8820::vortex_codec_en;
            r.codec_io = m8820::vortex_codec_io;
            r.spdif_flags = m8820::vortex_spdif_flags;
            r.irq_source = m8820::vortex_irq_source;
            r.irq_ctrl = m8820::vortex_irq_ctrl;
            r.irq_stat = m8820::vortex_irq_stat;
            r.ctrl = m8820::vortex_ctrl;
            r.engine_ctrl = m8820::vortex_engine_ctrl;
            r.midi_data = m8820::vortex_midi_data;
            r.midi_cmd = m8820::vortex_midi_cmd;
            r.game_legacy = m8820::vortex_game_legacy;
            r.game_axis = m8820::vortex_game_axis;
            break;
        case Au88x0Model::Au8830:
            r.adbdma_stat = m8830::vortex_adbdma_stat;
            r.adbdma_ctrl = m8830::vortex_adbdma_ctrl;
            r.adbdma_bufcfg0 = m8830::vortex_adbdma_bufcfg0;
            r.adbdma_bufcfg1 = m8830::vortex_adbdma_bufcfg1;
            r.adbdma_bufbase = m8830::vortex_adbdma_bufbase;
            r.adbdma_start = m8830::vortex_adbdma_start;
            r.adbdma_status = m8830::vortex_adbdma_status;
            r.adb_sr = m8830::vortex_adb_sr;
            r.adb_rtbase = m8830::vortex_adb_rtbase;
            r.adb_chnbase = m8830::vortex_adb_chnbase;
            r.mixer_sr = m8830::vortex_mixer_sr;
            r.mixer_chnbase = m8830::vortex_mixer_chnbase;
            r.mixer_rtbase = m8830::vortex_mixer_rtbase;
            r.mix_enin = m8830::vortex_mix_enin;
            r.mix_invol_a = m8830::vortex_mix_invol_a;
            r.mix_invol_b = m8830::vortex_mix_invol_b;
            r.mix_vol_a = m8830::vortex_mix_vol_a;
            r.mix_vol_b = m8830::vortex_mix_vol_b;
            r.src_chnbase = m8830::vortex_src_chnbase;
            r.src_rtbase = m8830::vortex_src_rtbase;
            r.src_convratio = m8830::vortex_src_convratio;
            r.fifo_adbctrl = m8830::vortex_fifo_adbctrl;
            r.fifo_wtctrl = m8830::vortex_fifo_wtctrl;
            r.fifo_adbdata = m8830::vortex_fifo_adbdata;
            r.fifo_wtdata = m8830::vortex_fifo_wtdata;
            r.codec_ctrl = m8830::vortex_codec_ctrl;
            r.codec_en = m8830::vortex_codec_en;
            r.codec_io = m8830::vortex_codec_io;
            r.spdif_flags = m8830::vortex_spdif_flags;
            r.irq_source = m8830::vortex_irq_source;
            r.irq_ctrl = m8830::vortex_irq_ctrl;
            r.irq_stat = m8830::vortex_irq_stat;
            r.ctrl = m8830::vortex_ctrl;
            r.engine_ctrl = m8830::vortex_engine_ctrl;
            r.midi_data = m8830::vortex_midi_data;
            r.midi_cmd = m8830::vortex_midi_cmd;
            r.game_legacy = m8830::vortex_game_legacy;
            r.game_axis = m8830::vortex_game_axis;
            break;
        default:
            break;
        }
    }

    // A fully-bound device: identity + vtable + runtime state.
    struct Au88x0Device {
        const Au88x0PciId *id;
        Au88x0Regs regs;
        Au88x0 chip;
    };

}  // namespace hype::chips::au88x0
