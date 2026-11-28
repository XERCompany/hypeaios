#pragma once

// AC'97 codec register map and NAMBAR/NABMBAR base layout.
//
// Reference: Audio Codec '97 Component Specification rev 2.3 and the
// register list used by Linux include/sound/ac97/regs.h. Values are
// rewritten as clean hcpp constants; layout matches the real codec
// register file byte-for-byte so drivers can index NAMBAR I/O space
// directly with these offsets.
//
// NAMBAR (Native Audio Mixer BAR): 256 bytes of codec registers,
// accessed as 16-bit I/O ports at even offsets.
// NABMBAR (Native Audio Bus Mastering BAR): 64 bytes of bus-master
// DMA registers, three 16-byte channels plus globals.

#include "hcpp/types.hpp"

namespace hype::chips::ac97 {

    // ---- NAMBAR codec register offsets (16-bit registers) ----------
    constexpr hcpp::u16 reg_reset = 0x00;           // Reset
    constexpr hcpp::u16 reg_master_volume = 0x02;   // Master Volume
    constexpr hcpp::u16 reg_headphone_volume = 0x04;  // Headphone/PCM Out
    constexpr hcpp::u16 reg_master_volume_mono = 0x06;
    constexpr hcpp::u16 reg_master_tone = 0x08;     // Tone control
    constexpr hcpp::u16 reg_pc_beep_volume = 0x0A;
    constexpr hcpp::u16 reg_phone_volume = 0x0C;
    constexpr hcpp::u16 reg_mic_volume = 0x0E;
    constexpr hcpp::u16 reg_line_in_volume = 0x10;
    constexpr hcpp::u16 reg_cd_volume = 0x12;
    constexpr hcpp::u16 reg_video_volume = 0x14;    // optional
    constexpr hcpp::u16 reg_aux_volume = 0x16;      // optional
    constexpr hcpp::u16 reg_pcm_out_volume = 0x18;
    constexpr hcpp::u16 reg_record_select = 0x1A;
    constexpr hcpp::u16 reg_record_gain = 0x1C;
    constexpr hcpp::u16 reg_record_gain_mic = 0x1E; // optional
    constexpr hcpp::u16 reg_general_purpose = 0x20; // optional
    constexpr hcpp::u16 reg_3d_control = 0x22;      // optional
    constexpr hcpp::u16 reg_int_paging = 0x24;      // AC'97 2.3
    constexpr hcpp::u16 reg_powerdown = 0x26;       // Powerdown ctrl/stat

    // AC'97 2.x audio extensions.
    constexpr hcpp::u16 reg_ext_audio_id = 0x28;    // Extended Audio ID
    constexpr hcpp::u16 reg_ext_audio_stat = 0x2A;  // Ext Audio Stat/Ctrl
    constexpr hcpp::u16 reg_pcm_front_dac_rate = 0x2C;
    constexpr hcpp::u16 reg_pcm_surround_dac_rate = 0x2E;
    constexpr hcpp::u16 reg_pcm_lfe_dac_rate = 0x30;
    constexpr hcpp::u16 reg_pcm_lr_adc_rate = 0x32;
    constexpr hcpp::u16 reg_pcm_mic_adc_rate = 0x34;
    constexpr hcpp::u16 reg_center_lfe_volume = 0x36;
    constexpr hcpp::u16 reg_surround_volume = 0x38;
    constexpr hcpp::u16 reg_spdif_control = 0x3A;

    // AC'97 modem extension registers.
    constexpr hcpp::u16 reg_ext_modem_id = 0x3C;
    constexpr hcpp::u16 reg_ext_modem_stat = 0x3E;
    constexpr hcpp::u16 reg_line1_rate = 0x40;
    constexpr hcpp::u16 reg_line2_rate = 0x42;
    constexpr hcpp::u16 reg_handset_rate = 0x44;
    constexpr hcpp::u16 reg_line1_level = 0x46;
    constexpr hcpp::u16 reg_line2_level = 0x48;
    constexpr hcpp::u16 reg_handset_level = 0x4A;
    constexpr hcpp::u16 reg_gpio_cfg = 0x4C;
    constexpr hcpp::u16 reg_gpio_polarity = 0x4E;
    constexpr hcpp::u16 reg_gpio_sticky = 0x50;
    constexpr hcpp::u16 reg_gpio_wakeup = 0x52;
    constexpr hcpp::u16 reg_gpio_status = 0x54;     // slot 12
    constexpr hcpp::u16 reg_misc_afe = 0x56;

    // 0x5A-0x7A vendor specific; the IDs close the map.
    constexpr hcpp::u16 reg_vendor_id1 = 0x7C;      // Vendor ID1
    constexpr hcpp::u16 reg_vendor_id2 = 0x7E;      // Vendor ID2 / rev

    // Extended codec register page 1 (paged via reg_int_paging).
    constexpr hcpp::u16 reg_codec_class_rev = 0x60;
    constexpr hcpp::u16 reg_pci_svid = 0x62;
    constexpr hcpp::u16 reg_pci_sid = 0x64;
    constexpr hcpp::u16 reg_func_select = 0x66;
    constexpr hcpp::u16 reg_func_info = 0x68;
    constexpr hcpp::u16 reg_sense_info = 0x6A;

    constexpr hcpp::u16 nambar_size = 0x100;        // 256-byte window
    constexpr hcpp::u16 nabmbar_size = 0x40;        // 64-byte window

    // ---- Volume/mute encodings ------------------------------------
    // Attenuation values are 1.5 dB steps; bit 15 (mono) or bit 15/7
    // (stereo L/R) mutes. Stereo regs use [12:8] L and [4:0] R.
    constexpr hcpp::u16 vol_mute_mono = 0x8000;
    constexpr hcpp::u16 vol_mute_stereo = 0x8080;
    constexpr hcpp::u16 vol_mask_stereo = 0x1F1F;
    constexpr hcpp::u16 vol_mask_mono = 0x001F;
    constexpr hcpp::u8 vol_steps = 0x1F;            // 0 = +0dB .. 31
    constexpr hcpp::u16 vol_gain_step_15db = 15;    // tenths of a dB

    inline hcpp::u16 vol_stereo(hcpp::u8 left, hcpp::u8 right,
                                bool mute = false) noexcept {
        return static_cast<hcpp::u16>((mute ? vol_mute_stereo : 0) |
                                      ((left & 0x1F) << 8) | (right & 0x1F));
    }

    // Record-select source codes (reg_record_select, both bytes).
    constexpr hcpp::u8 rec_src_mic = 0x00;
    constexpr hcpp::u8 rec_src_cd = 0x01;
    constexpr hcpp::u8 rec_src_video = 0x02;
    constexpr hcpp::u8 rec_src_aux = 0x03;
    constexpr hcpp::u8 rec_src_line_in = 0x04;
    constexpr hcpp::u8 rec_src_mix = 0x05;          // stereo mix
    constexpr hcpp::u8 rec_src_mono_mix = 0x06;
    constexpr hcpp::u8 rec_src_phone = 0x07;

    // ---- Reset register basic capabilities (read back from 0x00) ---
    constexpr hcpp::u16 bc_dedicated_mic = 0x0001;
    constexpr hcpp::u16 bc_reserved1 = 0x0002;
    constexpr hcpp::u16 bc_bass_treble = 0x0004;
    constexpr hcpp::u16 bc_sim_stereo = 0x0008;
    constexpr hcpp::u16 bc_headphone_out = 0x0010;
    constexpr hcpp::u16 bc_loudness = 0x0020;
    constexpr hcpp::u16 bc_dac_mask = 0x00C0;       // 16/18/20-bit DAC
    constexpr hcpp::u16 bc_dac_18bit = 0x0040;
    constexpr hcpp::u16 bc_dac_20bit = 0x0080;
    constexpr hcpp::u16 bc_adc_mask = 0x0300;       // 16/18/20-bit ADC
    constexpr hcpp::u16 bc_adc_18bit = 0x0100;
    constexpr hcpp::u16 bc_adc_20bit = 0x0200;
    constexpr hcpp::u16 bc_3d_id_mask = 0x7C00;

    // ---- General purpose register ---------------------------------
    constexpr hcpp::u16 gp_drss_mask = 0x0C00;      // double-rate slots
    constexpr hcpp::u16 gp_drss_10_11 = 0x0000;     // LR(C) on 10+11(+12)
    constexpr hcpp::u16 gp_drss_7_8 = 0x0400;       // LR on 7+8

    // ---- Powerdown register bits ----------------------------------
    constexpr hcpp::u16 pd_adc_status = 0x0001;     // RO
    constexpr hcpp::u16 pd_dac_status = 0x0002;     // RO
    constexpr hcpp::u16 pd_mixer_status = 0x0004;   // RO
    constexpr hcpp::u16 pd_vref_status = 0x0008;    // RO
    constexpr hcpp::u16 pd_ready_mask = 0x000F;     // all 4 set = ready
    constexpr hcpp::u16 pd_pr0 = 0x0100;            // PCM ADCs + mux
    constexpr hcpp::u16 pd_pr1 = 0x0200;            // PCM front DAC
    constexpr hcpp::u16 pd_pr2 = 0x0400;            // mixer, Vref on
    constexpr hcpp::u16 pd_pr3 = 0x0800;            // mixer, Vref off
    constexpr hcpp::u16 pd_pr4 = 0x1000;            // AC-link down
    constexpr hcpp::u16 pd_pr5 = 0x2000;            // internal clock off
    constexpr hcpp::u16 pd_pr6 = 0x4000;            // headphone amp
    constexpr hcpp::u16 pd_eapd = 0x8000;           // external amp pwr dn

    // ---- Extended Audio ID (0x28) ----------------------------------
    constexpr hcpp::u16 ei_vra = 0x0001;            // variable rate audio
    constexpr hcpp::u16 ei_dra = 0x0002;            // double-rate audio
    constexpr hcpp::u16 ei_spdif = 0x0004;          // S/PDIF out
    constexpr hcpp::u16 ei_vrm = 0x0008;            // variable rate MIC
    constexpr hcpp::u16 ei_dacs_slot_mask = 0x0030; // surround DAC slots
    constexpr hcpp::u8 ei_dacs_slot_shift = 4;
    constexpr hcpp::u16 ei_cdac = 0x0040;           // center DAC present
    constexpr hcpp::u16 ei_sdac = 0x0080;           // surround DACs
    constexpr hcpp::u16 ei_ldac = 0x0100;           // LFE DAC
    constexpr hcpp::u16 ei_amap = 0x0200;           // slot map per ID
    constexpr hcpp::u16 ei_rev_mask = 0x0C00;
    constexpr hcpp::u16 ei_rev_22 = 0x0400;         // AC'97 2.2
    constexpr hcpp::u16 ei_rev_23 = 0x0800;         // AC'97 2.3
    constexpr hcpp::u8 ei_rev_shift = 10;
    constexpr hcpp::u16 ei_addr_mask = 0xC000;      // codec address 0-3
    constexpr hcpp::u8 ei_addr_shift = 14;

    // ---- Extended Audio Status/Control (0x2A) ----------------------
    constexpr hcpp::u16 ea_vra = 0x0001;
    constexpr hcpp::u16 ea_dra = 0x0002;
    constexpr hcpp::u16 ea_spdif = 0x0004;
    constexpr hcpp::u16 ea_vrm = 0x0008;
    constexpr hcpp::u16 ea_spsa_mask = 0x0030;      // surround DAC slots
    constexpr hcpp::u8 ea_spsa_shift = 4;
    constexpr hcpp::u16 ea_spsa_3_4 = 0x0000;
    constexpr hcpp::u16 ea_spsa_7_8 = 0x0010;
    constexpr hcpp::u16 ea_spsa_6_9 = 0x0020;
    constexpr hcpp::u16 ea_spsa_10_11 = 0x0030;
    constexpr hcpp::u16 ea_cdac_ready = 0x0040;     // RO
    constexpr hcpp::u16 ea_sdac_ready = 0x0080;     // RO
    constexpr hcpp::u16 ea_ldac_ready = 0x0100;     // RO
    constexpr hcpp::u16 ea_mdac_ready = 0x0200;     // MIC ADC, RO
    constexpr hcpp::u16 ea_spcv = 0x0400;           // S/PDIF cfg valid
    constexpr hcpp::u16 ea_pri = 0x0800;            // center DAC off
    constexpr hcpp::u16 ea_prj = 0x1000;            // surround DACs off
    constexpr hcpp::u16 ea_prk = 0x2000;            // LFE DAC off
    constexpr hcpp::u16 ea_prl = 0x4000;            // MIC ADC off

    // ---- S/PDIF control (0x3A) --------------------------------------
    constexpr hcpp::u16 sc_pro = 0x0001;
    constexpr hcpp::u16 sc_naudio = 0x0002;
    constexpr hcpp::u16 sc_copy = 0x0004;
    constexpr hcpp::u16 sc_pre = 0x0008;
    constexpr hcpp::u16 sc_cc_mask = 0x07F0;
    constexpr hcpp::u8 sc_cc_shift = 4;
    constexpr hcpp::u16 sc_l = 0x0800;
    constexpr hcpp::u16 sc_spsr_mask = 0x3000;
    constexpr hcpp::u8 sc_spsr_shift = 12;
    constexpr hcpp::u16 sc_spsr_44k = 0x0000;
    constexpr hcpp::u16 sc_spsr_48k = 0x2000;
    constexpr hcpp::u16 sc_spsr_32k = 0x3000;
    constexpr hcpp::u16 sc_drs = 0x4000;
    constexpr hcpp::u16 sc_v = 0x8000;

    // ---- Interrupt/paging (0x24) ------------------------------------
    constexpr hcpp::u16 pg_page_mask = 0x000F;
    constexpr hcpp::u16 pg_page_vendor = 0x0000;
    constexpr hcpp::u16 pg_page_1 = 0x0001;
    constexpr hcpp::u16 pg_int_enable = 0x0800;
    constexpr hcpp::u16 pg_int_sense = 0x1000;
    constexpr hcpp::u16 pg_cause_sense = 0x2000;
    constexpr hcpp::u16 pg_cause_gpio = 0x4000;
    constexpr hcpp::u16 pg_int_status = 0x8000;

    // ---- NABMBAR bus-master register layout (Intel ICH-style) ------
    // Three PCM DMA channels at 16-byte strides, then globals. Each
    // channel block is: BDBAR(4) CIV(1) LVI(1) SR(2) PICB(2) PIV(1)
    // CR(1) at offsets 0x00/0x04/0x05/0x06/0x08/0x0A/0x0B.
    constexpr hcpp::u16 nabm_pcm_in_base = 0x00;
    constexpr hcpp::u16 nabm_pcm_out_base = 0x10;
    constexpr hcpp::u16 nabm_mic_in_base = 0x20;
    constexpr hcpp::u16 nabm_chan_stride = 0x10;

    // Channel-relative offsets inside each 16-byte block.
    constexpr hcpp::u8 nabm_bdbar = 0x00;           // u32 desc table
    constexpr hcpp::u8 nabm_civ = 0x04;             // current index
    constexpr hcpp::u8 nabm_lvi = 0x05;             // last valid index
    constexpr hcpp::u8 nabm_sr = 0x06;              // u16 status
    constexpr hcpp::u8 nabm_picb = 0x08;            // u16 pos in buffer
    constexpr hcpp::u8 nabm_piv = 0x0A;             // prefetched index
    constexpr hcpp::u8 nabm_cr = 0x0B;              // control

    // Global registers past the three channel blocks.
    constexpr hcpp::u16 nabm_glob_cnt = 0x2C;       // u32 global control
    constexpr hcpp::u16 nabm_glob_sta = 0x30;       // u32 global status
    constexpr hcpp::u16 nabm_cas = 0x34;            // codec access sema

    // Channel control register (CR) bits.
    constexpr hcpp::u8 cr_rpbm = 0x01;              // run/pause bus mast
    constexpr hcpp::u8 cr_rr = 0x02;                // reset registers
    constexpr hcpp::u8 cr_lvbie = 0x04;             // last-valid int
    constexpr hcpp::u8 cr_ioce = 0x10;              // IOC enable
    constexpr hcpp::u8 cr_fifoe = 0x08;             // FIFO-error int

    // Channel status register (SR) bits.
    constexpr hcpp::u16 sr_dch = 0x0001;            // DMA controller halt
    constexpr hcpp::u16 sr_celp = 0x0002;           // current == last
    constexpr hcpp::u16 sr_lvbci = 0x0004;          // LVBC interrupt
    constexpr hcpp::u16 sr_bcis = 0x0008;           // buffer-complete int
    constexpr hcpp::u16 sr_fifo = 0x0010;           // FIFO error

    // Global control (GLOB_CNT) bits.
    constexpr hcpp::u32 gc_6ch_vra = 0x00000001;    // PCM-out mode bit
    constexpr hcpp::u32 gc_cold_reset = 0x00000002; // AC-link cold reset
    constexpr hcpp::u32 gc_warm_reset = 0x00000004;
    constexpr hcpp::u32 gc_gie = 0x00000001;        // gpio int enable
    constexpr hcpp::u32 gc_4ch = 0x00000020;        // surround on
    constexpr hcpp::u32 gc_6ch = 0x00000040;        // center+LFE on
    constexpr hcpp::u32 gc_sri_mask = 0x00003000;
    constexpr hcpp::u32 gc_pri_int = 0x00040000;
    constexpr hcpp::u32 gc_sec_int = 0x00080000;
    constexpr hcpp::u32 gc_ter_int = 0x00100000;
    constexpr hcpp::u32 gc_ac97_off = 0x00800000;

    // Global status (GLOB_STA) bits.
    constexpr hcpp::u32 gs_gsci = 0x00000001;       // GPIO status change
    constexpr hcpp::u32 gs_mint = 0x00000002;       // modem-in int
    constexpr hcpp::u32 gs_point = 0x00000004;      // PCM-out int
    constexpr hcpp::u32 gs_piint = 0x00000020;      // PCM-in int
    constexpr hcpp::u32 gs_moint = 0x00000004;      // mic-in int (alias)
    constexpr hcpp::u32 gs_pri_ready = 0x00000100;  // primary codec rdy
    constexpr hcpp::u32 gs_sec_ready = 0x00000200;
    constexpr hcpp::u32 gs_ter_ready = 0x00000400;
    constexpr hcpp::u32 gs_pcr = 0x00000100;
    constexpr hcpp::u32 gs_scr = 0x00000200;
    constexpr hcpp::u32 gs_tcr = 0x00000400;
    constexpr hcpp::u32 gs_sample_cap = 0x0000C000; // 4/6 chan support
    constexpr hcpp::u32 gs_md3 = 0x00010000;
    constexpr hcpp::u32 gs_ad3 = 0x00020000;
    constexpr hcpp::u32 gs_bitclk = 0x04000000;     // bitclock status
    constexpr hcpp::u32 gs_ri3 = 0x08000000;        // resume int (ter)
    constexpr hcpp::u32 gs_ri2 = 0x10000000;
    constexpr hcpp::u32 gs_ri1 = 0x20000000;
    constexpr hcpp::u32 gs_ri0 = 0x40000000;

    // Buffer descriptor (BD) — 8 bytes, 32-entry table at BDBAR.
    constexpr hcpp::u8 bd_count = 32;
    constexpr hcpp::u16 bd_flag_ioc = 0x8000;       // interrupt on done
    constexpr hcpp::u16 bd_flag_bup = 0x4000;       // last-buffer flag
    constexpr hcpp::u16 bd_len_mask = 0x3FFF;       // samples, 16-bit

    struct Ac97Bd {
        hcpp::u32 address;      // physical, 4-byte aligned
        hcpp::u16 length;       // sample count (frames*channels)
        hcpp::u16 flags;        // bd_flag_*
    };
    static_assert(sizeof(Ac97Bd) == 8);

    // ---- Codec access helpers --------------------------------------
    // Front end to a controller-specific register transport (CAC on
    // ICH, codec-I/O regs on VIA/ALi, SDATA slots on AACI). Implement
    // behind an ops table in ac97bus.h.
    struct Ac97CodecIo {
        hcpp::u16 (*read)(void *ctx, hcpp::u8 codec_id, hcpp::u8 reg) noexcept;
        void (*write)(void *ctx, hcpp::u8 codec_id, hcpp::u8 reg,
                      hcpp::u16 value) noexcept;
        // Returns true once the codec reports powerdown ready bits.
        bool (*ready)(void *ctx, hcpp::u8 codec_id) noexcept;
        void *ctx;
    };

    inline hcpp::u16 codec_read(const Ac97CodecIo &io, hcpp::u8 codec_id,
                                hcpp::u8 reg) noexcept {
        return io.read(io.ctx, codec_id, reg & 0x7F);
    }

    inline void codec_write(const Ac97CodecIo &io, hcpp::u8 codec_id,
                            hcpp::u8 reg, hcpp::u16 value) noexcept {
        io.write(io.ctx, codec_id, reg & 0x7F, value);
    }

    inline hcpp::u32 codec_id_decode(hcpp::u16 id1, hcpp::u16 id2) noexcept {
        // 0x7C/0x7E pack a 24-bit vendor string + 8-bit revision:
        // id1 = vendor[23:8], id2 = vendor[7:0] << 8 | rev.
        return (static_cast<hcpp::u32>(id1) << 16) |
               (static_cast<hcpp::u32>(id2) & 0xFF00u);
    }

    inline hcpp::u8 codec_id_revision(hcpp::u16 id2) noexcept {
        return static_cast<hcpp::u8>(id2 & 0x00FF);
    }

}  // namespace hype::chips::ac97
