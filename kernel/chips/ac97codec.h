#pragma once

// AC'97 codec model: vendor ID decode, capability bits, per-codec
// state. A codec is identified by the 32-bit value composed from
// mixer registers 0x7C/0x7E: the upper 24 bits are a vendor string
// (e.g. 0x414C47 == "ALG" Realtek), the low 8 bits are a revision.
//
// Reference: Linux sound/pci/ac97/ac97_id.h vendor table and
// include/sound/ac97_codec.h scaps/caps field semantics.

#include "hcpp/types.hpp"
#include "kernel/chips/ac97base.h"

namespace hype::chips::ac97 {

    // ---- Codec vendor prefixes (top 24 bits of the codec id) -------
    constexpr hcpp::u32 vendor_analog_devices = 0x414453;  // "ADS"
    constexpr hcpp::u32 vendor_realtek = 0x414C47;         // "ALG"
    constexpr hcpp::u32 vendor_crystal = 0x435259;         // "CRY"
    constexpr hcpp::u32 vendor_conexant = 0x435854;        // "CXT"
    constexpr hcpp::u32 vendor_ice = 0x494345;             // "ICE"
    constexpr hcpp::u32 vendor_national = 0x4E5343;        // "NSC"
    constexpr hcpp::u32 vendor_tritech = 0x545241;         // "TRA"
    constexpr hcpp::u32 vendor_ti = 0x54584E;              // "TXN"
    constexpr hcpp::u32 vendor_via = 0x564941;             // "VIA"
    constexpr hcpp::u32 vendor_yamaha = 0x594D48;          // "YMH"
    constexpr hcpp::u32 vendor_sigmatel = 0x838476;        // STAC97xx
    constexpr hcpp::u32 vendor_cmedia = 0x434D49;          // "CMI"
    constexpr hcpp::u32 vendor_ess = 0x455353;             // "ESS"
    constexpr hcpp::u32 vendor_wolfson = 0x574D4C;         // "WML"

    // Frequently-seen full codec IDs (vendor << 8 | model nibble).
    constexpr hcpp::u32 id_ad1819a = 0x41445303;
    constexpr hcpp::u32 id_ad1881 = 0x41445340;
    constexpr hcpp::u32 id_ad1885 = 0x41445360;
    constexpr hcpp::u32 id_ad1886 = 0x41445361;
    constexpr hcpp::u32 id_ad1980 = 0x41445370;
    constexpr hcpp::u32 id_ad1981a = 0x41445372;
    constexpr hcpp::u32 id_ad1985 = 0x41445375;
    constexpr hcpp::u32 id_stac9700 = 0x83847600;
    constexpr hcpp::u32 id_stac9704 = 0x83847604;
    constexpr hcpp::u32 id_stac9721 = 0x83847609;
    constexpr hcpp::u32 id_stac9750 = 0x83847650;
    constexpr hcpp::u32 id_stac9758 = 0x83847658;
    constexpr hcpp::u32 id_stac9766 = 0x83847666;
    constexpr hcpp::u32 id_alc650 = 0x414C4720;
    constexpr hcpp::u32 id_alc202 = 0x414C4710;
    constexpr hcpp::u32 id_alc250 = 0x414C4752;
    constexpr hcpp::u32 id_alc655 = 0x414C4760;
    constexpr hcpp::u32 id_alc850 = 0x414C4790;
    constexpr hcpp::u32 id_cs4205 = 0x43525910;
    constexpr hcpp::u32 id_cs4297a = 0x43525928;
    constexpr hcpp::u32 id_cs4299 = 0x43525930;
    constexpr hcpp::u32 id_lm4550 = 0x4E534331;
    constexpr hcpp::u32 id_vt1612a = 0x56494161;
    constexpr hcpp::u32 id_vt1616 = 0x56494151;
    constexpr hcpp::u32 id_vt1617 = 0x56494152;
    constexpr hcpp::u32 id_ymf753 = 0x594D4803;

    // ---- Soft capability flags (mirroring ALSA scaps) --------------
    constexpr hcpp::u32 scap_audio = 0x00000001;      // audio codec
    constexpr hcpp::u32 scap_modem = 0x00000002;      // modem codec
    constexpr hcpp::u32 scap_surround_dac = 0x00000004;
    constexpr hcpp::u32 scap_center_lfe_dac = 0x00000008;
    constexpr hcpp::u32 scap_skip_audio = 0x00000010;
    constexpr hcpp::u32 scap_skip_modem = 0x00000020;
    constexpr hcpp::u32 scap_indep_sdin = 0x00000040;
    constexpr hcpp::u32 scap_inv_eapd = 0x00000080;
    constexpr hcpp::u32 scap_detect_by_vendor = 0x00000100;
    constexpr hcpp::u32 scap_no_spdif = 0x00000200;
    constexpr hcpp::u32 scap_eapd_led = 0x00000400;
    constexpr hcpp::u32 scap_power_save = 0x00000800;

    // Ac97Codec — one probed codec on a link. `caps` is the raw reset-
    // register bitfield (bc_*), `ext_id` is register 0x28 (ei_*), `id`
    // is the composed vendor id (codec_id_decode()). read/write go
    // through the link's transport ops so the same struct works on
    // ICH, VIA, ALi, AACI and emulated links.
    struct Ac97Codec {
        Ac97CodecIo io;         // transport bound at probe time
        hcpp::u8 num;           // codec address 0-3 (SDIN bit index)
        hcpp::u16 caps;         // reg 0x00 basic capabilities
        hcpp::u16 ext_id;       // reg 0x28 extended audio id
        hcpp::u16 ext_stat;     // reg 0x2A status/control shadow
        hcpp::u32 id;           // vendor id + revision
        hcpp::u32 scaps;        // soft capability flags (scap_*)
        bool present;
    };

    constexpr hcpp::u8 ac97_max_codecs = 4;

    inline hcpp::u32 codec_vendor_of(hcpp::u32 id) noexcept {
        return id & 0xFFFFFF00u;
    }

    inline bool codec_has_vra(const Ac97Codec &c) noexcept {
        return (c.ext_id & ei_vra) != 0;
    }

    inline bool codec_is_rev23(const Ac97Codec &c) noexcept {
        return (c.ext_id & ei_rev_mask) >= ei_rev_23;
    }

    inline bool codec_has_spdif(const Ac97Codec &c) noexcept {
        return (c.ext_id & ei_spdif) != 0 && (c.scaps & scap_no_spdif) == 0;
    }

    // Fill `codec` from a live link: probe presence via the powerdown
    // status bits, then latch caps + ids. Transport errors leave
    // present=false. The implementation lives in the link layer; this
    // is the canonical probe sequence every driver shares.
    inline bool codec_probe(Ac97Codec &c) noexcept {
        if (c.io.read == nullptr || c.io.write == nullptr) {
            c.present = false;
            return false;
        }
        c.caps = codec_read(c.io, c.num, reg_reset);
        c.id = codec_id_decode(codec_read(c.io, c.num, reg_vendor_id1),
                               codec_read(c.io, c.num, reg_vendor_id2));
        c.ext_id = codec_read(c.io, c.num, reg_ext_audio_id);
        c.ext_stat = codec_read(c.io, c.num, reg_ext_audio_stat);
        // A dead SDIN line reads back 0x0000 or 0xFFFF IDs.
        c.present = c.id != 0 && c.id != 0xFFFFFF00;
        return c.present;
    }

    // Enable/disable VRA + DRA by rewriting 0x2A.
    inline void codec_set_vra(Ac97Codec &c, bool vra, bool dra) noexcept {
        hcpp::u16 v = c.ext_stat;
        v = static_cast<hcpp::u16>(v & ~(ea_vra | ea_dra));
        if (vra && (c.ext_id & ei_vra)) v |= ea_vra;
        if (dra && (c.ext_id & ei_dra)) v |= ea_dra;
        codec_write(c.io, c.num, reg_ext_audio_stat, v);
        c.ext_stat = v;
    }

}  // namespace hype::chips::ac97
