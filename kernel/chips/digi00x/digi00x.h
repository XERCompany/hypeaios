#pragma once

// Digidesign Digi 002 / Digi 003 family — FireWire audio interfaces
// driven over AMDTP plus a vendor-specific asynchronous control
// protocol addressed at DG00X_ADDR_BASE. The device exposes its
// streaming state, clock source, sampling rate and optical-interface
// mode through quadlet registers; streaming is started by writing
// the isochronous channel pair and then the STREAMING_SET register.
//
// Reference: Linux sound/firewire/digi00x/digi00x.h (register
// offsets, rate/clock enums) and digi00x-transaction.c.

#include "hcpp/types.hpp"

namespace hype::chips::digi00x {

    // ---- IEEE 1212 identity ----------------------------------------------
    // Vendor OUI + model IDs as reported in config ROM.
    constexpr hcpp::u32 vendor_digidesign = 0x00A07E;
    constexpr hcpp::u32 model_console = 0x000001;   // Digi 002 console
    constexpr hcpp::u32 model_rack = 0x000002;      // Digi 002/003 rack
    constexpr hcpp::u32 spec_version = 0x000001;

    // ---- CSR control space -----------------------------------------------
    // All control registers live at this 64-bit base on the node.
    constexpr hcpp::u64 dg00x_addr_base = 0xFFFFE0000000ULL;

    constexpr hcpp::u16 reg_streaming_state = 0x0000;  // RO streaming
    constexpr hcpp::u16 reg_streaming_set = 0x0004;    // WO stream ctrl
    constexpr hcpp::u16 reg_message_addr = 0x0014;     // async msg ptr
    constexpr hcpp::u16 reg_isoc_channels = 0x0100;    // tx<<16 | rx
    constexpr hcpp::u16 reg_local_rate = 0x0110;       // rate set
    constexpr hcpp::u16 reg_external_rate = 0x0114;    // ext clock rate
    constexpr hcpp::u16 reg_clock_source = 0x0118;
    constexpr hcpp::u16 reg_opt_iface_mode = 0x011C;   // ADAT/S/PDIF
    constexpr hcpp::u16 reg_detect_external = 0x012C;  // RO ext sync
    constexpr hcpp::u16 reg_mmc = 0x0400;              // MIDI machine
                                                       // control

    // ---- Sampling rates (reg_local_rate / reg_external_rate value) -------
    enum class Dg00xRate : hcpp::u8 {
        R44100 = 0,
        R48000 = 1,
        R88200 = 2,
        R96000 = 3,
    };
    inline constexpr hcpp::u32 dg00x_rates[] = {
        44100, 48000, 88200, 96000,
    };
    constexpr hcpp::u8 dg00x_rate_count =
        sizeof(dg00x_rates) / sizeof(dg00x_rates[0]);

    // ---- Clock sources (reg_clock_source value) ---------------------------
    enum class Dg00xClock : hcpp::u8 {
        Internal = 0,
        Spdif = 1,
        Adat = 2,
        WordClock = 3,
    };

    // ---- Optical interface mode (reg_opt_iface_mode) -----------------------
    enum class Dg00xOptMode : hcpp::u8 {
        Adat = 0,
        Spdif = 1,
    };

    // ---- Streaming ----------------------------------------------------------
    // STREAMING_STATE returns 1 while isochronous streaming runs.
    // Session begin: write tx_ch<<16|rx_ch to ISOC_CHANNELS then 1 to
    // STREAMING_SET; finish writes 0x00000003 to STREAMING_SET.
    constexpr hcpp::u32 stream_begin = 0x00000001;
    constexpr hcpp::u32 stream_finish = 0x00000003;

    constexpr hcpp::u32 isoc_channels_word(hcpp::u16 tx_ch,
                                           hcpp::u16 rx_ch) noexcept {
        return (static_cast<hcpp::u32>(tx_ch) << 16) | rx_ch;
    }

    // ---- PCM channel counts ----------------------------------------------------
    // Data-block layout: 8 analog + 8 ADAT + 2 S/PDIF at <=48 kHz;
    // ADAT drops out in SMUX at >=88.2 kHz leaving 8+2.
    constexpr hcpp::u8 pcm_ch_normal = 18;   // 8 + 8 + 2
    constexpr hcpp::u8 pcm_ch_smux = 10;     // 8 + 2
    constexpr hcpp::u8 midi_ports = 3;       // port1/port2/console

    inline hcpp::u8 pcm_channels(Dg00xRate r) noexcept {
        return (r == Dg00xRate::R44100 || r == Dg00xRate::R48000)
                   ? pcm_ch_normal
                   : pcm_ch_smux;
    }

    // ---- MIDI tagging ------------------------------------------------------------
    // The first AMDTP data channel carries multiplexed MIDI; tag field
    // selects the port. Tag 0x0 = port1, 0x2 = port2, 0xE = console.
    constexpr hcpp::u8 midi_tag_port1 = 0x0;
    constexpr hcpp::u8 midi_tag_port2 = 0x2;
    constexpr hcpp::u8 midi_tag_console = 0xE;
    constexpr hcpp::u8 midi_channel_index = 0;

    // ---- Model table -------------------------------------------------------------
    struct Dg00xModel {
        hcpp::u32 vendor;
        hcpp::u32 model;
        const char *name;
        bool console;
    };

    inline constexpr Dg00xModel dg00x_models[] = {
        {vendor_digidesign, model_console, "Digi 002 Console", true},
        {vendor_digidesign, model_rack, "Digi 002/003 Rack", false},
    };
    constexpr hcpp::size_t dg00x_model_count =
        sizeof(dg00x_models) / sizeof(dg00x_models[0]);

    inline const Dg00xModel *dg00x_model_lookup(hcpp::u32 vendor,
                                                hcpp::u32 model) noexcept {
        for (hcpp::size_t i = 0; i < dg00x_model_count; ++i) {
            if (dg00x_models[i].vendor == vendor &&
                dg00x_models[i].model == model) {
                return &dg00x_models[i];
            }
        }
        return nullptr;
    }

}  // namespace hype::chips::digi00x
