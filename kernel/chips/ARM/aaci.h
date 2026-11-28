#pragma once

// ARM PrimeCell AACI (Advanced Audio Codec Interface, PL041).
// Register map from the PL041 Technical Reference Manual (ARM
// DDI0173) — the same layout used by Linux sound/arm/aaci.c.
//
// The block has four channel slots (CSCH1-4, 20 bytes each), a
// slot-interface register cluster for AC-link codec access, four
// DMA-accessible data FIFOs and the PrimeCell ID tail in core.h.

#include "hcpp/types.hpp"
#include "kernel/chips\ARM\core.h"

namespace hype::chips::arm::aaci {

    // ---- Channel-slot base offsets (each 0x14 bytes of regs) -------
    constexpr hcpp::u32 csch1 = 0x000;
    constexpr hcpp::u32 csch2 = 0x014;
    constexpr hcpp::u32 csch3 = 0x028;
    constexpr hcpp::u32 csch4 = 0x03C;

    // Per-channel register offsets (add to the CSCHn base).
    constexpr hcpp::u32 rxcr = 0x000;      // Rx control (29 bits)
    constexpr hcpp::u32 txcr = 0x004;      // Tx control (17 bits)
    constexpr hcpp::u32 sr = 0x008;        // status (12 bits)
    constexpr hcpp::u32 isr = 0x00C;       // interrupt status (7 bits)
    constexpr hcpp::u32 ie = 0x010;        // interrupt enable (7 bits)
    constexpr hcpp::u32 csch_stride = 0x014;

    // ---- Slot-interface registers ----------------------------------
    constexpr hcpp::u32 sl1rx = 0x050;     // slot 1 received data
    constexpr hcpp::u32 sl1tx = 0x054;     // slot 1 transmit data
    constexpr hcpp::u32 sl2rx = 0x058;
    constexpr hcpp::u32 sl2tx = 0x05C;
    constexpr hcpp::u32 sl12rx = 0x060;    // slot 12 received data
    constexpr hcpp::u32 sl12tx = 0x064;
    constexpr hcpp::u32 slfr = 0x068;      // slot flags
    constexpr hcpp::u32 slistat = 0x06C;   // slot interrupt status
    constexpr hcpp::u32 slien = 0x070;     // slot interrupt enable
    constexpr hcpp::u32 intclr = 0x074;    // interrupt clear
    constexpr hcpp::u32 maincr = 0x078;    // main control
    constexpr hcpp::u32 reset = 0x07C;     // reset control
    constexpr hcpp::u32 sync = 0x080;      // sync control
    constexpr hcpp::u32 allints = 0x084;   // all fifo interrupt status
    constexpr hcpp::u32 mainfr = 0x088;    // main flag register

    // DMA data FIFOs (16 words each, one per channel).
    constexpr hcpp::u32 dr1 = 0x090;       // fifo 1 data
    constexpr hcpp::u32 dr2 = 0x0B0;
    constexpr hcpp::u32 dr3 = 0x0D0;
    constexpr hcpp::u32 dr4 = 0x0F0;
    constexpr hcpp::u32 dr_stride = 0x020;
    constexpr hcpp::u32 fifo_words = 8;    // 32-byte fifo, u32 view

    // ---- RXCR/TXCR control register bits ---------------------------
    constexpr hcpp::u32 cr_fen = 1u << 16;      // fifo enable
    constexpr hcpp::u32 cr_compact = 1u << 15;  // compact mode
    constexpr hcpp::u32 cr_sz_mask = 3u << 13;
    constexpr hcpp::u32 cr_sz16 = 0u << 13;
    constexpr hcpp::u32 cr_sz18 = 1u << 13;
    constexpr hcpp::u32 cr_sz20 = 2u << 13;
    constexpr hcpp::u32 cr_sz12 = 3u << 13;
    // Slot enables: bit n enables AC-link slot n (1..12).
    constexpr hcpp::u32 cr_sl12 = 1u << 12;
    constexpr hcpp::u32 cr_sl11 = 1u << 11;
    constexpr hcpp::u32 cr_sl10 = 1u << 10;
    constexpr hcpp::u32 cr_sl9 = 1u << 9;
    constexpr hcpp::u32 cr_sl8 = 1u << 8;
    constexpr hcpp::u32 cr_sl7 = 1u << 7;
    constexpr hcpp::u32 cr_sl6 = 1u << 6;
    constexpr hcpp::u32 cr_sl5 = 1u << 5;
    constexpr hcpp::u32 cr_sl4 = 1u << 4;
    constexpr hcpp::u32 cr_sl3 = 1u << 3;
    constexpr hcpp::u32 cr_sl2 = 1u << 2;
    constexpr hcpp::u32 cr_sl1 = 1u << 1;
    constexpr hcpp::u32 cr_en = 1u << 0;       // channel enable
    inline hcpp::u32 cr_slot_bit(hcpp::u8 slot) noexcept {
        return 1u << slot;
    }

    // ---- Status register (SR) bits ---------------------------------
    constexpr hcpp::u32 sr_rxtofe = 1u << 11;   // rx timeout fifo empty
    constexpr hcpp::u32 sr_txto = 1u << 10;     // rx timeout fifo !empty
    constexpr hcpp::u32 sr_txu = 1u << 9;       // tx underrun
    constexpr hcpp::u32 sr_rxo = 1u << 8;       // rx overrun
    constexpr hcpp::u32 sr_txb = 1u << 7;       // tx busy
    constexpr hcpp::u32 sr_rxb = 1u << 6;       // rx busy
    constexpr hcpp::u32 sr_txff = 1u << 5;      // tx fifo full
    constexpr hcpp::u32 sr_rxff = 1u << 4;      // rx fifo full
    constexpr hcpp::u32 sr_txhe = 1u << 3;      // tx fifo half empty
    constexpr hcpp::u32 sr_rxhf = 1u << 2;      // rx fifo half full
    constexpr hcpp::u32 sr_txfe = 1u << 1;      // tx fifo empty
    constexpr hcpp::u32 sr_rxfe = 1u << 0;      // rx fifo empty

    // ---- Interrupt status/enable (ISR/IE) bits ---------------------
    constexpr hcpp::u32 int_rxtofe = 1u << 6;   // rx timeout fifo empty
    constexpr hcpp::u32 int_ur = 1u << 5;       // tx fifo underrun
    constexpr hcpp::u32 int_or = 1u << 4;       // rx fifo overrun
    constexpr hcpp::u32 int_rx = 1u << 3;       // rx fifo interrupt
    constexpr hcpp::u32 int_tx = 1u << 2;       // tx fifo interrupt
    constexpr hcpp::u32 int_rxto = 1u << 1;     // rx timeout
    constexpr hcpp::u32 int_txc = 1u << 0;      // tx complete
    constexpr hcpp::u32 int_all = 0x7F;

    // ---- Slot flag register (SLFR) bits ----------------------------
    constexpr hcpp::u32 slfr_rwis = 1u << 13;   // raw wake-up int status
    constexpr hcpp::u32 slfr_rgpiointr = 1u << 12;
    constexpr hcpp::u32 slfr_12txe = 1u << 11;  // slot 12 tx empty
    constexpr hcpp::u32 slfr_12rxv = 1u << 10;  // slot 12 rx valid
    constexpr hcpp::u32 slfr_2txe = 1u << 9;
    constexpr hcpp::u32 slfr_2rxv = 1u << 8;
    constexpr hcpp::u32 slfr_1txe = 1u << 7;
    constexpr hcpp::u32 slfr_1rxv = 1u << 6;
    constexpr hcpp::u32 slfr_12txb = 1u << 5;   // slot 12 tx busy
    constexpr hcpp::u32 slfr_12rxb = 1u << 4;
    constexpr hcpp::u32 slfr_2txb = 1u << 3;
    constexpr hcpp::u32 slfr_2rxb = 1u << 2;
    constexpr hcpp::u32 slfr_1txb = 1u << 1;
    constexpr hcpp::u32 slfr_1rxb = 1u << 0;
    constexpr hcpp::u32 slfr_slot_busy_mask =
        slfr_1txb | slfr_2txb | slfr_12txb;

    // ---- Interrupt clear (INTCLR) bits ------------------------------
    constexpr hcpp::u32 iclr_rxtofec4 = 1u << 12;
    constexpr hcpp::u32 iclr_rxtofec3 = 1u << 11;
    constexpr hcpp::u32 iclr_rxtofec2 = 1u << 10;
    constexpr hcpp::u32 iclr_rxtofec1 = 1u << 9;
    constexpr hcpp::u32 iclr_txuec4 = 1u << 8;
    constexpr hcpp::u32 iclr_txuec3 = 1u << 7;
    constexpr hcpp::u32 iclr_txuec2 = 1u << 6;
    constexpr hcpp::u32 iclr_txuec1 = 1u << 5;
    constexpr hcpp::u32 iclr_rxoec4 = 1u << 4;
    constexpr hcpp::u32 iclr_rxoec3 = 1u << 3;
    constexpr hcpp::u32 iclr_rxoec2 = 1u << 2;
    constexpr hcpp::u32 iclr_rxoec1 = 1u << 1;
    constexpr hcpp::u32 iclr_wisc = 1u << 0;

    // ---- Main control (MAINCR) bits ---------------------------------
    inline hcpp::u32 maincr_scra(hcpp::u32 codec) noexcept {
        return codec << 10;                 // secondary codec select
    }
    constexpr hcpp::u32 maincr_dmaen = 1u << 9;
    constexpr hcpp::u32 maincr_sl12txen = 1u << 8;
    constexpr hcpp::u32 maincr_sl12rxen = 1u << 7;
    constexpr hcpp::u32 maincr_sl2txen = 1u << 6;
    constexpr hcpp::u32 maincr_sl2rxen = 1u << 5;
    constexpr hcpp::u32 maincr_sl1txen = 1u << 4;
    constexpr hcpp::u32 maincr_sl1rxen = 1u << 3;
    constexpr hcpp::u32 maincr_lpm = 1u << 2;
    constexpr hcpp::u32 maincr_loopbk = 1u << 1;
    constexpr hcpp::u32 maincr_ie = 1u << 0;

    // ---- Reset / sync / main flag ----------------------------------
    constexpr hcpp::u32 reset_nrst = 1u << 0;
    constexpr hcpp::u32 sync_force = 1u << 0;
    constexpr hcpp::u32 mainfr_txb = 1u << 1;
    constexpr hcpp::u32 mainfr_rxb = 1u << 0;

    // ---- AC'97 codec register protocol -----------------------------
    // Codec access uses slots 1 (command), 2 (write data/read echo)
    // and 12 (GPIO). Encoding used by the slot-FIFO words:
    //   SL1TX = (reg << 12) | (data << 4)   for writes
    //   SL1TX = (reg << 12) | (1 << 19)     for reads  (bit19 = R/W)
    //   SL2TX = data << 4                   (companion write)
    // Readback: SL1RX = addr echo << 12 | SLOTREQ, SL2RX = data << 4.
    constexpr hcpp::u32 ac97_slot1_read_bit = 1u << 19;
    inline hcpp::u32 ac97_sl1_write(hcpp::u8 reg, hcpp::u16 data) noexcept {
        return (static_cast<hcpp::u32>(reg & 0x7F) << 12) |
               (static_cast<hcpp::u32>(data) << 4);
    }
    inline hcpp::u32 ac97_sl1_read(hcpp::u8 reg) noexcept {
        return (static_cast<hcpp::u32>(reg & 0x7F) << 12) |
               ac97_slot1_read_bit;
    }
    inline hcpp::u32 ac97_sl2_data(hcpp::u16 data) noexcept {
        return static_cast<hcpp::u32>(data) << 4;
    }
    inline hcpp::u8 ac97_sl1_index(hcpp::u32 sl1rx_v) noexcept {
        return static_cast<hcpp::u8>((sl1rx_v >> 12) & 0x7F);
    }
    inline hcpp::u16 ac97_sl2_value(hcpp::u32 sl2rx_v) noexcept {
        return static_cast<hcpp::u16>(sl2rx_v >> 4);
    }

    // PCM channel ids used with the four CSCH blocks (Linux ACSTREAM_*).
    enum class Stream : hcpp::u8 {
        Front = 0,
        Surround = 1,
        Lfe = 2,
    };

    // AACI device descriptor.
    struct Aaci {
        AmbaDevice dev;         // periphid must be periphid_aaci_pl041
        hcpp::u32 maincr;       // cached MAINCR shadow
        hcpp::u32 fifo_depth;   // words per DR fifo
    };

    inline bool aaci_supported(const AmbaDevice &d) noexcept {
        return d.periphid == periphid_aaci_pl041;
    }

}  // namespace hype::chips::arm::aaci
