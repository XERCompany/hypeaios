#pragma once

// Terratec AW2 family — card model table. The EWS88 MT/D / EWX24/96
// boards all use the SAA7146 PCI bridge; what differs is the codec
// chain on the GPIO expansion (AK4524 or ICE1712-derived front-end),
// the number of analog in/out pairs and whether a digital (coax/
// optical) interface is wired.
//
// Reference: Linux sound/pci/aw2/aw2-alsa.c snd_aw2_ids table and the
// board-variant detection in aw2-tsl.c.

#include "hcpp/types.hpp"
#include "kernel/chips/aw2-saa7146.h"

namespace hype::chips::aw2 {

    // ---- PCI identities ------------------------------------------------------
    constexpr hcpp::u16 pci_vendor_terratec = 0x153B;
    constexpr hcpp::u16 pci_vendor_saa7146 = 0x1131;   // Philips
    constexpr hcpp::u16 pci_dev_ews88mt = 0x1150;
    constexpr hcpp::u16 pci_dev_ews88d = 0x1151;
    constexpr hcpp::u16 pci_dev_ewx2496 = 0x115D;

    // ---- Codec front-ends ----------------------------------------------------
    enum class Aw2Codec : hcpp::u8 {
        Ak4524,        // AKM stereo codec pair (EWX)
        Cs8415a,       // Cirrus digital in/out
        Adat,          // optical ADAT pipe
        None,
    };

    // One row per PCI board. `analog`/`digital` count the stream
    // directions the SAA7146 TSL must carry.
    struct Aw2Card {
        hcpp::u16 device;
        const char *name;
        hcpp::u8 analog_in;
        hcpp::u8 analog_out;
        bool has_digital;
        Aw2Codec codec;
    };

    inline constexpr Aw2Card aw2_cards[] = {
        {pci_dev_ews88mt, "Terratec EWS88 MT", 8, 8, false,
         Aw2Codec::Ak4524},
        {pci_dev_ews88d, "Terratec EWS88 D", 6, 2, true,
         Aw2Codec::Cs8415a},
        {pci_dev_ewx2496, "Terratec EWX 24/96", 4, 4, true,
         Aw2Codec::Ak4524},
    };
    constexpr hcpp::size_t aw2_card_count =
        sizeof(aw2_cards) / sizeof(aw2_cards[0]);

    inline const Aw2Card *aw2_card_lookup(hcpp::u16 vendor,
                                          hcpp::u16 device) noexcept {
        if (vendor != pci_vendor_terratec &&
            vendor != pci_vendor_saa7146) return nullptr;
        for (hcpp::size_t i = 0; i < aw2_card_count; ++i) {
            if (aw2_cards[i].device == device) return &aw2_cards[i];
        }
        return nullptr;
    }

    // ---- Stream geometry ------------------------------------------------------
    // AW2 carries PCM on the audio serial ports only: S16_LE at
    // 44.1 kHz. Two playback streams (analog on stream 0, digital on
    // stream 1) and one capture stream per card.
    constexpr hcpp::u8 streams_playback = 2;
    constexpr hcpp::u8 streams_capture = 1;
    constexpr hcpp::u8 stream_analog = 0;
    constexpr hcpp::u8 stream_digital = 1;
    constexpr hcpp::u32 sample_rate_hz = 44100;
    constexpr hcpp::u8 sample_bits = 16;
    constexpr hcpp::u8 frame_bytes = 2;            // per channel
    constexpr hcpp::u32 buffer_max = 32768;        // bytes, per DMA buf
    constexpr hcpp::u8 dma_channels = 2;           // A1 + A2

    // Time-slot allocation: the SAA7146 TSL words pack 4 6-bit slot
    // numbers each; 8 active slots carry the analog codec lanes.
    constexpr hcpp::u8 tsl_active_slots = 8;
    constexpr hcpp::u8 tsl_bits_per_slot = 6;

    constexpr hcpp::u32 tsl_pack(hcpp::u8 s0, hcpp::u8 s1, hcpp::u8 s2,
                                 hcpp::u8 s3) noexcept {
        return (static_cast<hcpp::u32>(s0) & 0x3F) |
               ((static_cast<hcpp::u32>(s1) & 0x3F) << 8) |
               ((static_cast<hcpp::u32>(s2) & 0x3F) << 16) |
               ((static_cast<hcpp::u32>(s3) & 0x3F) << 24);
    }

}  // namespace hype::chips::aw2
