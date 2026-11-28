#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Creative EMU10k1/EMU10k2 family (SB Live!, Audigy, Audigy2).
// Register offsets and the init sequence mirror Linux ALSA
// sound/pci/emu10k1 (emu10k1.h / emu10k1_main.c / io.c):
//  - I/O BAR0 hosts a flat register file plus an indexed
//    ("indirect") window accessed via PTR=(reg<<16)|channel then
//    DATA.
//  - AC'97 codec access goes through the AC97ADDRESS/AC97DATA ports.
//
// What works here: PCI identify, BAR0 claim, chip-level init
// (cache lock + interrupt mask + capture buffer reset + codec
// reset + master volume + audio enable readback).
// What is intentionally left offline: the voice/DMA engine — it
// needs the host page-table (PTB) + MAPA/MAPB routing and a full
// channel program (CCCA/PSST/DSL/CURRADDR etc.) that is beyond
// this wave; engine_ready stays false and the gap is documented.

namespace hype::audio::emu10k1 {

    constexpr hcpp::u16 vendor_creative = 0x1102;
    constexpr hcpp::u16 device_live10k1 = 0x0002;   // SB Live! EMU10k1
    constexpr hcpp::u16 device_audigy = 0x0004;     // Audigy EMU10k2
    constexpr hcpp::u16 device_audigy2 = 0x0008;    // Audigy2/EMU10k2.5

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // I/O register window (BAR0).
    constexpr hcpp::u8 reg_ptr = 0x00;          // u32 indexed pointer
    constexpr hcpp::u8 reg_data = 0x04;         // u32 indexed data
    constexpr hcpp::u8 reg_ipr = 0x08;          // u32 interrupt pending
    constexpr hcpp::u8 reg_inte = 0x0c;         // u32 interrupt enable
    constexpr hcpp::u8 reg_wc = 0x10;           // u32 wall clock
    constexpr hcpp::u8 reg_hcfg = 0x14;         // u32 hardware config
    constexpr hcpp::u8 reg_ac97data = 0x1c;     // u16 AC'97 data
    constexpr hcpp::u8 reg_ac97address = 0x1e;  // u8 AC'97 register index
    constexpr hcpp::u8 reg_hcfg2 = 0x34;        // u32 config (10k2)

    constexpr hcpp::u8 ac97_ready = 0x80;       // AC97ADDRESS ready bit

    // HCFG bits used by snd_emu10k1_init().
    constexpr hcpp::u32 hcfg_locksoundcache = 0x00000008;
    constexpr hcpp::u32 hcfg_locktankcache = 0x00000004;
    constexpr hcpp::u32 hcfg_mutebuttonenable = 0x00000002;
    constexpr hcpp::u32 hcfg_audioenable = 0x00000001;

    // Indirect (indexed) registers, selected via PTR/DATA.
    constexpr hcpp::u8 ireg_mapa = 0x0c;        // cache map A
    constexpr hcpp::u8 ireg_mapb = 0x0d;        // cache map B
    constexpr hcpp::u8 ireg_ptb = 0x40;         // page table base
    constexpr hcpp::u8 ireg_tcb = 0x41;         // tank cache base
    constexpr hcpp::u8 ireg_tcbs = 0x44;        // tank cache size
    constexpr hcpp::u8 ireg_micba = 0x45;       // mic buffer address
    constexpr hcpp::u8 ireg_adcba = 0x46;       // ADC buffer address
    constexpr hcpp::u8 ireg_fxba = 0x47;        // FX buffer address
    constexpr hcpp::u8 ireg_micbs = 0x49;       // mic buffer size
    constexpr hcpp::u8 ireg_adcbs = 0x4a;       // ADC buffer size
    constexpr hcpp::u8 ireg_fxbs = 0x4b;        // FX buffer size
    constexpr hcpp::u8 ireg_cliel = 0x58;       // chan loop irq en lo
    constexpr hcpp::u8 ireg_clieh = 0x59;       // chan loop irq en hi
    constexpr hcpp::u8 ireg_solel = 0x5c;       // stop-on-loop en lo
    constexpr hcpp::u8 ireg_soleh = 0x5d;       // stop-on-loop en hi

    constexpr hcpp::u32 adcbs_bufsize_none = 0;
    constexpr hcpp::u32 tcbs_bufsize_16k = 0;

    // AC'97 codec registers (standard AC'97 numbering).
    constexpr hcpp::u8 ac97_reset = 0x00;
    constexpr hcpp::u8 ac97_master_volume = 0x02;
    constexpr hcpp::u8 ac97_pcmout_volume = 0x18;
    constexpr hcpp::u8 ac97_vendor_id1 = 0x7c;
    constexpr hcpp::u8 ac97_vendor_id2 = 0x7e;

    constexpr hcpp::u8 voice_count = 64;
    constexpr hcpp::u8 ring_pages = 4;

    struct Emu10k1Controller {
        hcpp::u16 iobase;
        hcpp::u16 device_id;
        bool audigy;
        bool reset_done;
        bool engine_ready;
        hcpp::u16 codec_vendor;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *emu10k1_find() noexcept;
    bool emu10k1_bind(const pci::PciDevice &device,
                      Emu10k1Controller &controller) noexcept;
    bool emu10k1_reset(Emu10k1Controller &controller) noexcept;
    bool emu10k1_codec_write(Emu10k1Controller &controller, hcpp::u8 reg,
                             hcpp::u16 value) noexcept;
    bool emu10k1_present() noexcept;
    bool emu10k1_probe() noexcept;

}  // namespace hype::audio::emu10k1
