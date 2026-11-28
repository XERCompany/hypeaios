#pragma once

// Aureal Vortex wavetable synthesizer — AU8820/AU8830 only.
// Voices are grouped in two banks (AU8830: 0x20 voices/bank, 0x40
// total); each voice is described by a fixed-size register page
// inside VORTEX_WT_BASE + per-voice DMA FIFOs (WTDMA engines).
//
// The WT engine plays ROM/RAM sample-based voices routed into the
// ADB mixer through the WT out nodes (offset_wt0/offset_wt1).
//
// Reference: Linux sound/pci/au88x0/au88x0_wt.h and the WT register
// computation macros used in au88x0_pcm.c / au88x0_synth.c.

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0core.h"

namespace hype::chips::au88x0 {

    // ---- Voice geometry -----------------------------------------------------
    constexpr hcpp::u8 wt_banks = 2;
    constexpr hcpp::u8 wt_voices_per_bank = 0x20;
    constexpr hcpp::u8 wt_voice_reg_size = 0x10;    // registers per voice
    constexpr hcpp::u8 wt_voice_reg_bytes = 0x40;   // stride in MMIO

    // ---- Per-voice register map (offsets within a voice page) -----------------
    // Mirrors the layout Linux programs in vortex_wt_SetReg(): the
    // "this" control word block, LFO state, envelope generators, and
    // the sample-play position counters.
    constexpr hcpp::u32 wt_voice_ctrl = 0x00;      // run/stop + flags
    constexpr hcpp::u32 wt_voice_lfo1 = 0x04;
    constexpr hcpp::u32 wt_voice_lfo2 = 0x08;
    constexpr hcpp::u32 wt_voice_env = 0x0C;       // envelope phase/rate
    constexpr hcpp::u32 wt_voice_pos = 0x10;       // sample position
    constexpr hcpp::u32 wt_voice_loop = 0x14;      // loop start/end
    constexpr hcpp::u32 wt_voice_vol = 0x18;       // channel volumes
    constexpr hcpp::u32 wt_voice_route = 0x1C;     // ADB dest node

    // Voice control bits.
    constexpr hcpp::u32 wt_run = 0x00000001;
    constexpr hcpp::u32 wt_loop_en = 0x00000002;
    constexpr hcpp::u32 wt_16bit = 0x00000004;
    constexpr hcpp::u32 wt_stereo = 0x00000008;
    constexpr hcpp::u32 wt_int_en = 0x00000010;

    // Compute a voice register offset: bank*voices + voice selects the
    // page, then the field offset inside the page (Linux WT_* macros).
    inline hcpp::u32 wt_voice_offset(hcpp::u8 bank, hcpp::u8 voice,
                                     hcpp::u32 field) noexcept {
        return (static_cast<hcpp::u32>(
                    (bank * wt_voices_per_bank + voice)) *
                wt_voice_reg_bytes) + field;
    }

    // WTDMA engine selection: engine index == bank*voices + voice
    // (a WTDMA channel feeds exactly one voice's sample stream).
    inline hcpp::u8 wt_dma_of(hcpp::u8 bank, hcpp::u8 voice) noexcept {
        return static_cast<hcpp::u8>(bank * wt_voices_per_bank + voice);
    }

    // WT source node on the ADB: bank picks offset_wt0/wt1, voice
    // picks the route index inside the bank (Linux ADB_WTOUT()).
    constexpr hcpp::u8 wt_bank0_out_base = 0x40;   // == m8830::offset_wt0
    constexpr hcpp::u8 wt_bank1_out_base = 0x80;   // == m8830::offset_wt1
    inline hcpp::u8 wt_out_node(hcpp::u8 bank, hcpp::u8 voice) noexcept {
        return static_cast<hcpp::u8>(
            voice + (bank == 0 ? wt_bank0_out_base
                               : wt_bank1_out_base));
    }

    // Sample-rate conversion for WT playback is done by dedicating
    // one SRC slice to the WT submixer; the engine uses the shared
    // VORTEX_SRC_* block (a-au88xx.h).

}  // namespace hype::chips::au88x0
