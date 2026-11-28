#pragma once

// AMDTP packet codec — IEC 61883-6 "Audio and Music Data Transmission
// Protocol" carried in CIP packets (IEC 61883-1) on an isochronous
// channel. This module implements the two-quadlet CIP header
// (sid/dbs/fn-qpc-sph/dbc + fmt/fdf/syt) and AM824 data blocks: one
// quadlet per channel, label byte in the top octet, 24-bit big-endian
// MBLA sample in the low three octets.
//
// Sample format in/out of the codec: hcpp::i32 holding a signed 24-bit
// value (range -8388608..8388607), i.e. full-scale PCM *without* the
// low-byte padding. Callers convert to/from s16 with >> 8.

#include "hcpp/types.hpp"

namespace hype::fw {

    // ---- CIP header bit fields (quadlet 0) ----------------------------
    // [31]=eoh(0), [29:24]=sid, [23:16]=dbs, [15:14]=fn, [13:11]=qpc,
    // [10]=sph, [9:8]=reserved, [7:0]=dbc
    constexpr hcpp::u32 cip_eoh = 0x80000000u;
    constexpr hcpp::u32 cip_sid_mask = 0x3F000000u;
    constexpr hcpp::u32 cip_dbs_mask = 0x00FF0000u;
    constexpr hcpp::u32 cip_fn_mask = 0x0000C000u;
    constexpr hcpp::u32 cip_qpc_mask = 0x00003800u;
    constexpr hcpp::u32 cip_sph = 0x00000400u;
    constexpr hcpp::u32 cip_dbc_mask = 0x000000FFu;

    // ---- CIP header bit fields (quadlet 1) ----------------------------
    // [31]=eoh(1), [29:24]=fmt, [23:16]=fdf, [15:0]=syt
    constexpr hcpp::u32 cip_fmt_mask = 0x3F000000u;
    constexpr hcpp::u32 cip_fdf_mask = 0x00FF0000u;
    constexpr hcpp::u32 cip_syt_mask = 0x0000FFFFu;

    constexpr hcpp::u8 cip_fmt_am824 = 0x10;   // IEC 61883-6 stream
    constexpr hcpp::u8 amdtp_fdf_no_data = 0xFF;  // packet carries no blocks
    constexpr hcpp::u16 cip_syt_no_info = 0xFFFF;

    // AM824 data-block labels (top octet of every payload quadlet).
    constexpr hcpp::u8 amdtp_label_mbla24 = 0x40;   // multi-bit linear audio
    constexpr hcpp::u8 amdtp_label_iec958_1 = 0x00; // IEC958 channel A0
    constexpr hcpp::u8 amdtp_label_iec958_2 = 0x01; // IEC958 channel A1
    constexpr hcpp::u8 amdtp_label_midi = 0x80;     // MIDI conformant data
    constexpr hcpp::u8 amdtp_label_smp = 0xC0;      // SMP-TIME CODE
    constexpr hcpp::u8 amdtp_label_empty = 0x7F;    // no valid data

    // Sampling-frequency codes (FDF sfc field).
    constexpr hcpp::u8 sfc_32k = 0;
    constexpr hcpp::u8 sfc_44k1 = 1;
    constexpr hcpp::u8 sfc_48k = 2;
    constexpr hcpp::u8 sfc_88k2 = 3;
    constexpr hcpp::u8 sfc_96k = 4;
    constexpr hcpp::u8 sfc_176k4 = 5;
    constexpr hcpp::u8 sfc_192k = 6;
    constexpr hcpp::u8 sfc_count = 7;

    constexpr hcpp::u8 amdtp_max_dbs = 40;  // channels + midi quadlets
    constexpr hcpp::u32 amdtp_header_bytes = 8;  // two-quadlet CIP header

    struct AmdtpCip {
        hcpp::u8 sid;
        hcpp::u8 dbs;
        hcpp::u8 fn;
        hcpp::u8 qpc;
        bool sph;
        hcpp::u8 dbc;
        hcpp::u8 fmt;
        hcpp::u8 fdf;
        hcpp::u16 syt;
    };

    struct AmdtpCodec {
        hcpp::u8 channels;        // MBLA audio channels per data block
        hcpp::u8 midi_quadlets;   // one quadlet per MIDI port (0..8)
        hcpp::u8 dbs;             // channels + midi_quadlets
        hcpp::u8 sfc;             // sampling frequency code
        hcpp::u8 sid;             // source node id used when encoding
        hcpp::u32 rate;           // Hz
        hcpp::u8 tx_dbc;          // next data-block counter for encode
        hcpp::u8 rx_dbc_expected; // expected dbc on decode
        bool rx_dbc_valid;
        hcpp::u32 rx_seq_errors;  // dbc discontinuities seen
        hcpp::u32 rx_no_data;     // fdf == NO_DATA packets seen
        hcpp::u32 rx_dropped_labels;
        hcpp::u32 tx_packets;
        hcpp::u32 rx_packets;
    };

    hcpp::u8 amdtp_sfc_for_rate(hcpp::u32 rate) noexcept;
    hcpp::u32 amdtp_rate_for_sfc(hcpp::u8 sfc) noexcept;

    // Nominal data blocks per iso cycle (8000 cycles/s): exact for
    // 32/48/96/176.4/192 kHz; 44.1/88.2 alternate (6,5,5,5,5,5,5,6...)
    // in real streams — callers that transmit pick the count per cycle,
    // receivers read it back from the payload length.
    hcpp::u32 amdtp_nominal_blocks(hcpp::u32 rate) noexcept;

    void amdtp_codec_init(AmdtpCodec &codec, hcpp::u8 channels,
                          hcpp::u8 midi_quadlets, hcpp::u32 rate,
                          hcpp::u8 sid) noexcept;

    // Parse a two-quadlet CIP header from the first 8 bytes of a packet
    // payload (the bytes after the iso packet header quadlet). Returns
    // false when the framing is not a two-quadlet AM824 CIP header.
    bool amdtp_parse_cip(const hcpp::u8 *packet, hcpp::u32 len,
                         AmdtpCip &cip) noexcept;

    // Encode `data_blocks` data blocks (each `codec.channels` MBLA
    // quadlets + midi padding) into `out`. `pcm24` is interleaved
    // channels-major 24-bit samples; nullptr produces MBLA silence.
    // Returns payload bytes written (8 + dbs*4*data_blocks), or 0.
    hcpp::u32 amdtp_encode(AmdtpCodec &codec, const hcpp::i32 *pcm24,
                           hcpp::u32 data_blocks, hcpp::u8 *out,
                           hcpp::u32 out_cap) noexcept;

    // Decode one received packet payload (bytes after the iso header
    // quadlet): validates the CIP header, tracks dbc sequencing, and
    // unpacks MBLA24 quadlets into interleaved `pcm24`. Returns the
    // number of data blocks (sample frames) decoded, or 0 for NO_DATA
    // packets / framing errors.
    hcpp::u32 amdtp_decode(AmdtpCodec &codec, const hcpp::u8 *packet,
                           hcpp::u32 len, hcpp::i32 *pcm24,
                           hcpp::u32 max_frames) noexcept;

    // Round-trip selftest: encode a known PCM buffer and verify the
    // decode reproduces every sample and the dbc sequence. Pure
    // software; prints the "amdtp codec=ok|fail" marker.
    bool amdtp_codec_selftest() noexcept;

}  // namespace hype::fw
