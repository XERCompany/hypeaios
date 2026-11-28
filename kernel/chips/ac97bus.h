#pragma once

// AC'97 AC-link bus model: the 13-slot serial frame, output tag bits,
// input SLOTREQ flags, and the abstract bus/DMA ops (the "bbi" — bus
// bridge interface) shared by AC'97 controller drivers (Intel ICH,
// VIA 686/8233, SiS 7012, ARM AACI, nVidia/AMD AC'97 cores).
//
// Slot map follows the AC'97 2.3 spec §4.4; SLOTREQ bits are the
// active-low data-request flags in input-frame slot 1 (Status Address
// Port bits [11:2]) used for on-demand variable-rate delivery.

#include "hcpp/types.hpp"
#include "kernel/chips/ac97base.h"

namespace hype::chips::ac97 {

    // ---- AC-link slot numbers --------------------------------------
    constexpr hcpp::u8 slot_tag = 0;          // output/input tag
    constexpr hcpp::u8 slot_cmd_addr = 1;     // out: ctrl addr port
    constexpr hcpp::u8 slot_cmd_data = 2;     // out: ctrl data port
    constexpr hcpp::u8 slot_status_addr = 1;  // in: status addr echo
    constexpr hcpp::u8 slot_status_data = 2;  // in: status data
    constexpr hcpp::u8 slot_pcm_left = 3;
    constexpr hcpp::u8 slot_pcm_right = 4;
    constexpr hcpp::u8 slot_modem_line1 = 5;
    constexpr hcpp::u8 slot_pcm_center = 6;   // out center / in mic
    constexpr hcpp::u8 slot_mic_adc = 6;      // in: dedicated mic ADC
    constexpr hcpp::u8 slot_spdif_l1 = 6;     // S/PDIF left (primary)
    constexpr hcpp::u8 slot_pcm_left_0 = 7;   // double-rate alias
    constexpr hcpp::u8 slot_surround_left = 7;
    constexpr hcpp::u8 slot_spdif_left = 7;
    constexpr hcpp::u8 slot_pcm_right_0 = 8;  // double-rate alias
    constexpr hcpp::u8 slot_surround_right = 8;
    constexpr hcpp::u8 slot_spdif_right = 8;
    constexpr hcpp::u8 slot_lfe = 9;
    constexpr hcpp::u8 slot_spdif_r1 = 9;
    constexpr hcpp::u8 slot_modem_line2 = 10;
    constexpr hcpp::u8 slot_pcm_left_1 = 10;  // double-rate
    constexpr hcpp::u8 slot_spdif_l2 = 10;
    constexpr hcpp::u8 slot_handset = 11;     // out
    constexpr hcpp::u8 slot_pcm_right_1 = 11; // double-rate
    constexpr hcpp::u8 slot_spdif_r2 = 11;
    constexpr hcpp::u8 slot_modem_gpio = 12;
    constexpr hcpp::u8 slot_pcm_center_1 = 12;
    constexpr hcpp::u8 slot_count = 13;

    // ---- Output tag word (slot 0, SDATA_OUT) -----------------------
    // Bit15 = frame valid; bit14..3 = slots 1..12 contain valid data.
    constexpr hcpp::u16 tag_frame_valid = 0x8000;
    constexpr hcpp::u16 tag_slot1_valid = 0x4000;   // cmd addr
    constexpr hcpp::u16 tag_slot2_valid = 0x2000;   // cmd data
    inline hcpp::u16 tag_slot_valid_bit(hcpp::u8 slot) noexcept {
        // slot 3 -> bit12 ... slot 12 -> bit3
        return static_cast<hcpp::u16>(1u << (15 - slot));
    }
    constexpr hcpp::u16 tag_pcm_front = 0x1800;     // slots 3+4 valid
    constexpr hcpp::u16 tag_pcm_surround = 0x0180;  // slots 7+8
    constexpr hcpp::u16 tag_pcm_center_lfe = 0x0240; // slots 6+9 valid

    // ---- Input tag word (slot 0, SDATA_IN) -------------------------
    constexpr hcpp::u16 tag_codec_ready = 0x8000;
    // bits 14:3 = slot-valid mirror of output tag; bits 1:0 carry the
    // two LSBs of the codec's address echo (codec id readout).

    // ---- SLOTREQ (input slot 1 bits [11:2], active LOW) ------------
    // 0 = "send data for this slot in the next output frame"; the codec
    // drives these when VRA (ext-audio 0x2A bit0) is enabled.
    constexpr hcpp::u16 slotreq_mask = 0x0FFC;
    constexpr hcpp::u16 slotreq_slot3 = 0x0800;   // PCM left
    constexpr hcpp::u16 slotreq_slot4 = 0x0400;   // PCM right
    constexpr hcpp::u16 slotreq_slot5 = 0x0200;   // modem line 1
    constexpr hcpp::u16 slotreq_slot6 = 0x0100;   // PCM center
    constexpr hcpp::u16 slotreq_slot7 = 0x0080;   // PCM surround L
    constexpr hcpp::u16 slotreq_slot8 = 0x0040;   // PCM surround R
    constexpr hcpp::u16 slotreq_slot9 = 0x0020;   // PCM LFE
    constexpr hcpp::u16 slotreq_slot10 = 0x0010;  // modem line2 / L(n+1)
    constexpr hcpp::u16 slotreq_slot11 = 0x0008;  // handset / R(n+1)
    constexpr hcpp::u16 slotreq_slot12 = 0x0004;  // center (n+1)

    inline hcpp::u16 slotreq_for_slot(hcpp::u8 slot) noexcept {
        return static_cast<hcpp::u16>(1u << (14 - slot));
    }

    // ---- Command address/data port encodings -----------------------
    // Output slot 1: bit19 R/W (0=write,1=read), bits18:12 = reg index.
    constexpr hcpp::u32 cmd_addr_read = 0x00080000;  // bit19
    inline hcpp::u32 cmd_addr_pack(hcpp::u8 reg, bool read) noexcept {
        return (static_cast<hcpp::u32>(reg & 0x7F) << 12) |
               (read ? cmd_addr_read : 0);
    }
    // Input slot 1 echo: bits18:12 = index, bits11:2 = SLOTREQ.
    inline hcpp::u8 status_addr_index(hcpp::u32 slot1) noexcept {
        return static_cast<hcpp::u8>((slot1 >> 12) & 0x7F);
    }
    inline hcpp::u16 status_addr_slotreq(hcpp::u32 slot1) noexcept {
        return static_cast<hcpp::u16>(slot1 & slotreq_mask);
    }

    // ---- Ac97DmaChannel / bbi ops ----------------------------------
    // Each AC'97 link exports three half-duplex DMA engines: pcm-in,
    // pcm-out, mic-in. `Ac97Bbi` (bus bridge interface) is the ops
    // table every controller-specific driver fills in so upper layers
    // (mixer, PCM layer, selftests) stay vendor-neutral.

    enum class Ac97DmaChannel : hcpp::u8 {
        PcmIn = 0,
        PcmOut = 1,
        MicIn = 2,
    };

    struct Ac97DmaOps {
        // Program the BD list (physical address + slot count to arm).
        void (*set_table)(void *ctx, Ac97DmaChannel ch, hcpp::u32 bdbar_phys,
                          hcpp::u8 lvi) noexcept;
        void (*start)(void *ctx, Ac97DmaChannel ch) noexcept;
        void (*pause)(void *ctx, Ac97DmaChannel ch) noexcept;
        void (*reset)(void *ctx, Ac97DmaChannel ch) noexcept;
        // Current byte position inside the buffer (PICB-based).
        hcpp::u16 (*position)(void *ctx, Ac97DmaChannel ch) noexcept;
        // Clear BCIS/LVBCI status bits; returns raw SR.
        hcpp::u16 (*ack_irq)(void *ctx, Ac97DmaChannel ch) noexcept;
        void *ctx;
    };

    struct Ac97Bbi {
        Ac97CodecIo codec;              // register transport
        Ac97DmaOps dma;                 // shared DMA engine ops
        void (*cold_reset)(void *ctx) noexcept;
        void (*warm_reset)(void *ctx) noexcept;
        void *ctx;
        // Convenience wrappers that ignore the ops' private ctx when
        // the caller already holds it.
        void (*enter_low_power)(void *ctx) noexcept;
    };

    // The canonical front/surround/center-LFE slot grouping used by
    // both the interrupt handler and the DMA fill loop.
    struct Ac97PcmSlots {
        hcpp::u8 front_left;    // 3
        hcpp::u8 front_right;   // 4
        hcpp::u8 center;        // 6
        hcpp::u8 surround_l;    // 7
        hcpp::u8 surround_r;    // 8
        hcpp::u8 lfe;           // 9
    };

    inline constexpr Ac97PcmSlots ac97_pcm_slots_default{
        slot_pcm_left, slot_pcm_right, slot_pcm_center,
        slot_surround_left, slot_surround_right, slot_lfe};

    // Build the output-tag valid mask for an N-channel PCM frame.
    // channels: 2 front, 4 front+surround, 6 +center/lfe.
    inline hcpp::u16 tag_for_pcm_channels(hcpp::u8 channels) noexcept {
        hcpp::u16 tag = tag_frame_valid |
                        tag_slot_valid_bit(slot_pcm_left) |
                        tag_slot_valid_bit(slot_pcm_right);
        if (channels >= 4) {
            tag = static_cast<hcpp::u16>(
                tag | tag_slot_valid_bit(slot_surround_left) |
                tag_slot_valid_bit(slot_surround_right));
        }
        if (channels >= 6) {
            tag = static_cast<hcpp::u16>(
                tag | tag_slot_valid_bit(slot_pcm_center) |
                tag_slot_valid_bit(slot_lfe));
        }
        return tag;
    }

}  // namespace hype::chips::ac97
