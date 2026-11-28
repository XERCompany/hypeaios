#pragma once

// Aureal Vortex AC'97 codec interface: the VORTEX_CODEC_IO command
// word encoding, the busy-poll protocol, codec addresses, plus the
// Vortex-specific vendor registers used for stereo quad detection
// and headphone/crosstalk handling.
//
// Reference: Linux sound/pci/au88x0/au88x0_core.c
// (vortex_codec_write/vortex_codec_read, vortex_codec_init),
// au88x0_eq.c codecinit(), and the ac97 register set in
// kernel/chips/ac97codec.h.

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0core.h"
#include "kernel/chips/ac97codec.h"

namespace hype::chips::au88x0 {

    // ---- Codec I/O transaction protocol ------------------------------------
    // Write: program CODEC_IO with codec_io_pack(...,write=true), then
    // poll CODEC_CTRL's busy bit until clear.
    // Read: program CODEC_IO without the write flag, poll busy, take
    // the low 16 bits of CODEC_IO.
    constexpr hcpp::u32 codec_busy_tmo = 60000;   // poll iterations

    // Post one codec command word; caller then polls CODEC_CTRL for
    // the busy bit to clear before issuing the next transaction.
    inline void codec_io_send(hcpp::uintptr_t mmio, hcpp::u32 codec_io,
                              hcpp::u32 word) noexcept {
        hwwrite(mmio, codec_io, word);
    }
    // Poll helper: returns true once the codec controller is idle.
    inline bool codec_io_idle(hcpp::uintptr_t mmio,
                              hcpp::u32 codec_ctrl) noexcept {
        return (hwread(mmio, codec_ctrl) & codec_busy) == 0;
    }

    // The readback path clears bit 31 of CODEC_IO after a read before
    // the value is valid (Linux codec_read_data quirk).
    constexpr hcpp::u32 codec_rd_clear_bit = 0x80000000;

    // ---- Codec addresses ---------------------------------------------------
    constexpr hcpp::u8 codec_audio_id = 0;   // primary AC'97
    constexpr hcpp::u8 codec_modem_id = 1;   // AC'98 modem (8830)

    // Vortex codec reset: issue AC'97 warm reset then wait for
    // powerdown status bits to report ready (all 4 RO bits set).
    constexpr hcpp::u16 codec_reset_reg = ac97::reg_reset;
    constexpr hcpp::u16 codec_reset_ready_mask = ac97::pd_ready_mask;

    // Quad-speaker detection: SDAC bit in AC'97 Extended Audio ID.
    constexpr hcpp::u16 ext_id_sdac = 0x0080;
    constexpr hcpp::u16 ext_id_ldac = 0x0040;
    constexpr hcpp::u16 ext_id_amic = 0x0001;

    // Vortex-specific vendor register touched during codec init:
    // loopback cross-connect + headphone bistream decompression.
    constexpr hcpp::u8 ac97_vortex_loop_reg = 0x32;
    constexpr hcpp::u16 ac97_vortex_loop_hp = 0x0000;
    constexpr hcpp::u8 ac97_vortex_xtalk_reg = 0x70;
    constexpr hcpp::u16 ac97_vortex_xtalk_on = 0x0001;

    // Codec IRQ/enable defaults on AU8810 (bitstream mode quirk).
    constexpr hcpp::u16 ac97_vortex_xt_hp1 = 0xB3A7;
    constexpr hcpp::u16 ac97_vortex_xt_hp2 = 0x064E;
    constexpr hcpp::u16 ac97_vortex_xt_hp3 = 0x62C2;

    // Default codec init register set (AU8810/AU8830 path writes these
    // via codec_write for the "simple" no-EQ init).
    struct Au88x0CodecInitPair {
        hcpp::u8 reg;
        hcpp::u16 value;
    };
    inline constexpr Au88x0CodecInitPair codec_init_table[] = {
        {ac97::reg_master_volume, 0x0101},
        {ac97::reg_pcm_out_volume, 0x0808},
        {ac97::reg_pc_beep_volume, 0x8080},
        {ac97::reg_mic_volume, 0x8080},
        {ac97::reg_line_in_volume, 0x0808},
        {ac97::reg_cd_volume, 0x0808},
        {ac97::reg_aux_volume, 0x0808},
        {ac97::reg_record_select, 0x0404},
        {ac97::reg_record_gain, 0x8040},
        {ac97::reg_powerdown, 0x0000},
        {ac97::reg_ext_audio_id, 0x0001},
        {ac97::reg_ext_audio_stat, 0x0000},
    };
    constexpr hcpp::size_t codec_init_table_count =
        sizeof(codec_init_table) / sizeof(codec_init_table[0]);

}  // namespace hype::chips::au88x0
