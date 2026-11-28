#pragma once

// ALi M5451 device register map (BAR0 I/O window).
//
// Reference: Linux sound/pci/ali5451/ali5451.c "Direct Registers"
// block and the channel-window scheme: select a hardware voice with
// the GC_CIR index register, then its parameters appear in the
// 8-register window at 0xE0-0xFC (ALI_CHANNEL_REGS = 8).

#include "hcpp/types.hpp"
#include "kernel/chips/ali5451-pci.h"

namespace hype::chips::ali5451 {

    // ---- Voice/channel model ----------------------------------------
    constexpr hcpp::u8 channels = 32;           // hardware voices
    constexpr hcpp::u8 channel_regs = 8;        // window regs/voice
    constexpr hcpp::u8 global_regs = 56;        // 32-bit reg slots 0-0xDC

    // Reserved voice numbers (special channels inside the 32-voice
    // table): capture, digital in/out, surround bus.
    constexpr hcpp::u8 voice_pcm_in = 31;
    constexpr hcpp::u8 voice_spdif_in = 19;
    constexpr hcpp::u8 voice_spdif_out = 15;
    constexpr hcpp::u8 voice_center = 24;
    constexpr hcpp::u8 voice_lef = 23;
    constexpr hcpp::u8 voice_surr_left = 26;
    constexpr hcpp::u8 voice_surr_right = 25;
    constexpr hcpp::u8 voice_modem_in = 21;
    constexpr hcpp::u8 voice_modem_out = 20;

    constexpr hcpp::u8 voice_type_pcm = 0x01;
    constexpr hcpp::u8 voice_type_other = 0x02;

    // ---- Legacy decode registers ------------------------------------
    constexpr hcpp::u16 reg_legacy_dmar0 = 0x00;   // ADR0
    constexpr hcpp::u16 reg_legacy_dmar4 = 0x04;   // CNT0
    constexpr hcpp::u16 reg_legacy_dmar11 = 0x0B;  // MOD
    constexpr hcpp::u16 reg_legacy_dmar15 = 0x0F;  // MMR
    constexpr hcpp::u16 reg_mpu_cmd0 = 0x20;       // MPU-401 regs
    constexpr hcpp::u16 reg_mpu_cmd1 = 0x21;
    constexpr hcpp::u16 reg_mpu_cmd2 = 0x22;
    constexpr hcpp::u16 reg_mpu_cmd3 = 0x23;

    // ---- AC'97 codec access ------------------------------------------
    constexpr hcpp::u16 reg_ac97_write = 0x40;
    constexpr hcpp::u16 reg_ac97_read = 0x44;
    // Command word fields: [31:16]=data, [15]=busy/trigger,
    // [7]=secondary codec, [8]=V02 flag, [6:0]=register index.
    constexpr hcpp::u32 ac97_cmd_busy = 0x00008000;
    constexpr hcpp::u32 ac97_cmd_secondary = 0x00000080;
    constexpr hcpp::u32 ac97_cmd_v02 = 0x00000100;
    constexpr hcpp::u32 ac97_cmd_data_shift = 16;
    constexpr hcpp::u32 ac97_reg_mask = 0x7F;
    constexpr hcpp::u8 max_codecs = 2;
    // Poke packer: mirrors ali_codec_poke()'s dword construction.
    inline hcpp::u32 ac97_poke_word(hcpp::u8 reg, hcpp::u16 data,
                                    bool secondary, bool v02) noexcept {
        hcpp::u32 v = ac97_cmd_busy |
                      (static_cast<hcpp::u32>(data) << ac97_cmd_data_shift) |
                      (reg & 0xFF);
        if (secondary) v |= ac97_cmd_secondary;
        if (v02) v |= ac97_cmd_v02;
        return v;
    }
    inline hcpp::u16 ac97_peek_value(hcpp::u32 readback) noexcept {
        return static_cast<hcpp::u16>((readback >> 16) & 0xFFFF);
    }

    // ---- Serial/GPIO/SPDIF control -----------------------------------
    constexpr hcpp::u16 reg_sctrl = 0x48;
    constexpr hcpp::u32 sctrl_spdif_out_enable = 0x20;
    constexpr hcpp::u32 sctrl_line_in2 = 1u << 9;
    constexpr hcpp::u32 sctrl_gpio_in2 = 1u << 13;
    constexpr hcpp::u32 sctrl_line_out_en = 1u << 20;
    constexpr hcpp::u32 sctrl_gpio_out_en = 1u << 23;
    constexpr hcpp::u32 sctrl_codec1_ready = 1u << 24;
    constexpr hcpp::u32 sctrl_codec2_ready = 1u << 25;

    constexpr hcpp::u16 reg_ac97_gpio = 0x4C;
    constexpr hcpp::u32 ac97_gpio_enable = 0x8000;
    constexpr hcpp::u8 ac97_gpio_data_shift = 16;

    constexpr hcpp::u16 reg_spdif_cs = 0x70;
    constexpr hcpp::u16 reg_spdif_ctrl = 0x74;
    constexpr hcpp::u32 spdif_in_func_enable = 0x02;
    constexpr hcpp::u32 spdif_in_ch_status = 0x40;
    constexpr hcpp::u32 spdif_out_ch_status = 0xBF;

    // ---- Engine control / interrupt ----------------------------------
    constexpr hcpp::u16 reg_start = 0x80;          // voice start bitmap
    constexpr hcpp::u16 reg_stop = 0x84;           // voice stop bitmap
    constexpr hcpp::u16 reg_cspf = 0x90;           // current sample pos flags
    constexpr hcpp::u16 reg_aint = 0x98;           // active voice irq bitmap
    constexpr hcpp::u16 reg_gc_cir = 0xA0;         // channel-window index
    constexpr hcpp::u32 aint_endlp_ie = 0x00001000; // end-of-loop irq bit
    constexpr hcpp::u32 aint_midlp_ie = 0x00002000; // mid-point irq bit
    constexpr hcpp::u16 reg_ainten = 0xA4;         // irq-enable bitmap
    constexpr hcpp::u16 reg_volume = 0xA8;
    constexpr hcpp::u16 reg_sbdelta = 0xAC;        // SBDELTA_DELTA_R

    constexpr hcpp::u16 reg_miscint = 0xB0;
    constexpr hcpp::u32 miscint_address_irq = 0x00000020;
    constexpr hcpp::u32 miscint_target_reached = 0x00008000;
    constexpr hcpp::u32 miscint_mixer_overflow = 0x00000800;
    constexpr hcpp::u32 miscint_mixer_underflow = 0x00000400;
    constexpr hcpp::u32 miscint_gpio_irq = 0x01000000;

    constexpr hcpp::u16 reg_sbbl_sbcl = 0xC0;
    constexpr hcpp::u16 reg_sbctrl = 0xC4;         // SBCTRL_SBE2R_SBDD
    constexpr hcpp::u16 reg_stimer = 0xC8;         // codec-sync timer
    constexpr hcpp::u16 reg_global_control = 0xD4;
    constexpr hcpp::u32 gctrl_spdif_out_sel_pcm = 0x00000400;
    constexpr hcpp::u32 gctrl_spdif_in_support = 0x00000800;
    constexpr hcpp::u32 gctrl_spdif_out_ch_enable = 0x00008000;
    constexpr hcpp::u32 gctrl_spdif_in_ch_enable = 0x00080000;
    constexpr hcpp::u32 gctrl_pcm_in_enable = 0x80000000;
    // Bit per voice: enable it as a "special" (non-PCM) channel.
    inline hcpp::u32 gctrl_channel_bit(hcpp::u8 voice) noexcept {
        return 1u << (voice & 0x1F);
    }

    // ---- Channel-window registers (indexed via GC_CIR) ---------------
    constexpr hcpp::u16 reg_cso_alpha_fms = 0xE0;  // [31:16]=CSO, [15:0]=FMS
    constexpr hcpp::u16 reg_lba = 0xE4;            // loop base address
    constexpr hcpp::u16 reg_eso_delta = 0xE8;      // [31:16]=ESO, [15:0]=DELTA
    constexpr hcpp::u16 reg_gvsel_pan_voc_ctrl_ec = 0xF0;
    constexpr hcpp::u16 reg_ebuf1 = 0xF4;
    constexpr hcpp::u16 reg_ebuf2 = 0xF8;
    constexpr hcpp::u16 chan_window_base = 0xE0;

    // reg_gvsel_pan_voc_ctrl_ec field packing (ali_write_voice_regs):
    // [31]=GVSEL, [30:24]=PAN(7b), [23:16]=VOL(8b), [15:12]=CTRL(4b),
    // [11:0]=EC(12b).
    inline hcpp::u32 chan_ctrl_word(hcpp::u8 gvsel, hcpp::u8 pan,
                                    hcpp::u8 vol, hcpp::u8 ctrl,
                                    hcpp::u16 ec) noexcept {
        return (static_cast<hcpp::u32>(gvsel & 1) << 31) |
               ((static_cast<hcpp::u32>(pan) & 0x7F) << 24) |
               ((static_cast<hcpp::u32>(vol) & 0xFF) << 16) |
               ((static_cast<hcpp::u32>(ctrl) & 0x0F) << 12) |
               (ec & 0x0FFF);
    }
    constexpr hcpp::u32 ebuf_still_mode = 0x30000000;

    // GVSEL modes (volume select).
    constexpr hcpp::u8 gvsel_pcm = 0;
    constexpr hcpp::u8 gvsel_pan = 1;

    // The delta base for 48 kHz playback used by ali_convert_rate().
    constexpr hcpp::u32 delta_48k = 0x1000;

    // ---- Voice programming model --------------------------------------
    struct AliVoice {
        hcpp::u8 number;
        bool pcm;
        bool midi;
        bool mode;      // 0=stereo pack, 1=extra reg set
        bool synth;
        bool running;
        hcpp::i32 eso;  // end-sample offset (can be -1 for continuous)
        hcpp::i32 count;
    };

    struct AliVoiceRegs {
        hcpp::u32 cso_alpha_fms;
        hcpp::u32 lba;
        hcpp::u32 eso_delta;
        hcpp::u32 gvsel_pan_voc_ctrl_ec;
        hcpp::u32 ebuf1;
        hcpp::u32 ebuf2;
    };

    // suspend/resume image: global regs + per-voice window.
    struct AliStateImage {
        hcpp::u32 regs[global_regs];
        hcpp::u32 channel_regs[channels][channel_regs];
    };

    // GC_CIR index write selects which voice the window shows.
    inline hcpp::u8 gc_cir_index(hcpp::u8 voice) noexcept {
        return voice & 0x1F;
    }

}  // namespace hype::chips::ali5451
