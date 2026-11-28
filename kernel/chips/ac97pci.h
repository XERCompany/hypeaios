#pragma once

// AC'97 PCI glue: known controller vendor/device IDs and the
// per-vendor BAR/IO layout used to reach the codec registers.
//
// IDs match the tables in Linux sound/pci/intel8x0.c, via82xx.c and
// sis7019-era drivers. ICH-family parts expose NAMBAR on BAR0 and
// NABMBAR on BAR1; nForce folds NABM into BAR0+0x80; VIA uses a
// single BAR with the codec access register at offset 0x80.

#include "hcpp/types.hpp"

namespace hype::chips::ac97 {

    // ---- Vendor IDs -------------------------------------------------
    constexpr hcpp::u16 pci_vendor_intel = 0x8086;
    constexpr hcpp::u16 pci_vendor_sis = 0x1039;
    constexpr hcpp::u16 pci_vendor_via = 0x1106;
    constexpr hcpp::u16 pci_vendor_nvidia = 0x10DE;
    constexpr hcpp::u16 pci_vendor_amd = 0x1022;
    constexpr hcpp::u16 pci_vendor_ati = 0x1002;
    constexpr hcpp::u16 pci_vendor_ali = 0x10B9;
    constexpr hcpp::u16 pci_vendor_trident = 0x1023;

    // Intel ICH family AC'97 audio controllers.
    constexpr hcpp::u16 pci_dev_ich_82801aa = 0x2415;    // ICH
    constexpr hcpp::u16 pci_dev_ich_82801ab = 0x2425;    // ICH0
    constexpr hcpp::u16 pci_dev_ich_82801ba = 0x2445;    // ICH2
    constexpr hcpp::u16 pci_dev_ich_82801ca = 0x2485;    // ICH3-M
    constexpr hcpp::u16 pci_dev_ich_82801db = 0x24C5;    // ICH4
    constexpr hcpp::u16 pci_dev_ich_82801eb = 0x24D5;    // ICH5
    constexpr hcpp::u16 pci_dev_ich_6300esb = 0x25A6;    // ESB AC'97
    constexpr hcpp::u16 pci_dev_ich_82801fb = 0x266E;    // ICH6
    constexpr hcpp::u16 pci_dev_ich_82801gb = 0x27DE;    // ICH7
    constexpr hcpp::u16 pci_dev_ich_82440mx = 0x7195;    // 82440MX

    // SiS.
    constexpr hcpp::u16 pci_dev_sis_7012 = 0x7012;       // SiS 7012
    constexpr hcpp::u16 pci_dev_sis_7013 = 0x7013;       // SiS 7013 modem

    // VIA.
    constexpr hcpp::u16 pci_dev_via_82c686 = 0x3058;     // VT82C686A/B
    constexpr hcpp::u16 pci_dev_via_8233 = 0x3059;       // VT8233/A/C

    // nVidia nForce.
    constexpr hcpp::u16 pci_dev_nforce = 0x01B1;         // nForce
    constexpr hcpp::u16 pci_dev_nforce2 = 0x006A;        // nForce2 MCP-T
    constexpr hcpp::u16 pci_dev_ck804 = 0x0059;          // CK804
    constexpr hcpp::u16 pci_dev_nforce3 = 0x00DA;
    constexpr hcpp::u16 pci_dev_ck8s = 0x00EA;           // CK8S
    constexpr hcpp::u16 pci_dev_mcp04 = 0x0590;          // MCP04

    // AMD.
    constexpr hcpp::u16 pci_dev_amd768 = 0x7445;         // AMD-768
    constexpr hcpp::u16 pci_dev_amd8111 = 0x746D;        // AMD-8111

    // ATI IXP.
    constexpr hcpp::u16 pci_dev_ixp150 = 0x4341;
    constexpr hcpp::u16 pci_dev_ixp200 = 0x4342;
    constexpr hcpp::u16 pci_dev_ixp250 = 0x4378;
    constexpr hcpp::u16 pci_dev_ixp300 = 0x436D;
    constexpr hcpp::u16 pci_dev_ixp400 = 0x4370;
    constexpr hcpp::u16 pci_dev_sb450 = 0x4370;

    // ALi M5455 (native AC'97, distinct from the M5451 APU).
    constexpr hcpp::u16 pci_dev_ali5455 = 0x5455;

    // Trident 4DWave-DX/NX.
    constexpr hcpp::u16 pci_dev_trident_4dwave_dx = 0x2000;
    constexpr hcpp::u16 pci_dev_trident_4dwave_nx = 0x2001;
    constexpr hcpp::u16 pci_dev_sis_7018 = 0x7018;       // SiS 7018 audio

    // PCI class identifiers.
    constexpr hcpp::u8 pci_class_multimedia = 0x04;
    constexpr hcpp::u8 pci_subclass_audio = 0x01;        // AC'97 audio
    constexpr hcpp::u8 pci_subclass_modem = 0x03;        // AC'97 modem

    // ---- BAR layout styles -------------------------------------------
    enum class Ac97IoStyle : hcpp::u8 {
        NamNabm = 0,    // NAMBAR=BAR0, NABMBAR=BAR1 (ICH, SiS, ALi5455)
        SingleNabm = 1, // NAMBAR=BAR0, NABMBAR=BAR0+offset (nForce, AMD)
        DirectRegs = 2, // one BAR, codec window + DMA regs (VIA, IXP)
    };

    struct Ac97PciIoMap {
        Ac97IoStyle style;
        hcpp::u8 nam_bar;            // BAR carrying mixer regs
        hcpp::u8 nabm_bar;           // BAR carrying DMA regs (style 0)
        hcpp::u16 nabm_offset;       // offset inside nam_bar (style 1/2)
        hcpp::u16 codec_access_off;  // direct codec cmd port (style 2)
    };

    // ICH/VIA/SiS-style: two BARs.
    inline constexpr Ac97PciIoMap io_map_ich{
        Ac97IoStyle::NamNabm, 0, 1, 0x0000, 0x0000};
    // nForce/AMD8111: NABM folded into BAR0 at +0x80.
    inline constexpr Ac97PciIoMap io_map_nforce{
        Ac97IoStyle::SingleNabm, 0, 0, 0x0080, 0x0000};
    // VIA: BAR0; AC97 access port at +0x80, DMA channel regs at 0x00.
    inline constexpr Ac97PciIoMap io_map_via{
        Ac97IoStyle::DirectRegs, 0, 0, 0x0000, 0x0080};

    // VIA direct-register offsets (via82xx BAR0 window).
    constexpr hcpp::u16 via_reg_ac97_cmd = 0x80;    // codec cmd/status
    constexpr hcpp::u16 via_reg_status = 0x00;
    constexpr hcpp::u16 via_reg_control = 0x04;
    constexpr hcpp::u16 via_reg_intptr = 0x08;
    constexpr hcpp::u16 via_reg_table_base = 0x04;
    constexpr hcpp::u16 via_reg_stop = 0x08;
    constexpr hcpp::u16 via_reg_position = 0x0C;
    constexpr hcpp::u32 via_ac97_busy = 0x00000001;
    constexpr hcpp::u32 via_ac97_codec00 = 0x00000000;  // primary
    constexpr hcpp::u32 via_ac97_codec10 = 0x00200000;  // secondary
    constexpr hcpp::u32 via_ac97_read = 0x01000000;
    constexpr hcpp::u32 via_ac97_write = 0x00000000;
    constexpr hcpp::u32 via_ac97_index_shift = 16;
    constexpr hcpp::u32 via_ac97_data_shift = 0;

    // ---- Device table -----------------------------------------------
    // One row per known AC'97 controller. `io` picks the BAR strategy;
    // `ext` is family data (e.g. nForce has no MIC-in channel).
    struct Ac97PciId {
        hcpp::u16 vendor;
        hcpp::u16 device;
        const char *name;
        Ac97IoStyle style;
        hcpp::u8 max_codecs;
        bool has_spdif;
    };

    inline constexpr Ac97PciId ac97_pci_table[] = {
        {pci_vendor_intel, pci_dev_ich_82801aa, "ICH 82801AA",
         Ac97IoStyle::NamNabm, 3, false},
        {pci_vendor_intel, pci_dev_ich_82801ab, "ICH0 82801AB",
         Ac97IoStyle::NamNabm, 3, false},
        {pci_vendor_intel, pci_dev_ich_82801ba, "ICH2 82801BA",
         Ac97IoStyle::NamNabm, 3, false},
        {pci_vendor_intel, pci_dev_ich_82801ca, "ICH3 82801CA",
         Ac97IoStyle::NamNabm, 3, false},
        {pci_vendor_intel, pci_dev_ich_82801db, "ICH4 82801DB",
         Ac97IoStyle::NamNabm, 3, true},
        {pci_vendor_intel, pci_dev_ich_82801eb, "ICH5 82801EB",
         Ac97IoStyle::NamNabm, 3, true},
        {pci_vendor_intel, pci_dev_ich_6300esb, "6300ESB",
         Ac97IoStyle::NamNabm, 2, false},
        {pci_vendor_intel, pci_dev_ich_82801fb, "ICH6 82801FB",
         Ac97IoStyle::NamNabm, 3, true},
        {pci_vendor_intel, pci_dev_ich_82801gb, "ICH7 82801GB",
         Ac97IoStyle::NamNabm, 3, true},
        {pci_vendor_sis, pci_dev_sis_7012, "SiS 7012",
         Ac97IoStyle::NamNabm, 2, true},
        {pci_vendor_via, pci_dev_via_82c686, "VIA 82C686",
         Ac97IoStyle::DirectRegs, 2, false},
        {pci_vendor_via, pci_dev_via_8233, "VIA 8233",
         Ac97IoStyle::DirectRegs, 2, true},
        {pci_vendor_nvidia, pci_dev_nforce, "nForce",
         Ac97IoStyle::SingleNabm, 2, false},
        {pci_vendor_nvidia, pci_dev_nforce2, "nForce2",
         Ac97IoStyle::SingleNabm, 2, false},
        {pci_vendor_nvidia, pci_dev_ck804, "CK804",
         Ac97IoStyle::SingleNabm, 2, true},
        {pci_vendor_nvidia, pci_dev_ck8s, "CK8S",
         Ac97IoStyle::SingleNabm, 2, false},
        {pci_vendor_amd, pci_dev_amd768, "AMD-768",
         Ac97IoStyle::NamNabm, 2, false},
        {pci_vendor_amd, pci_dev_amd8111, "AMD-8111",
         Ac97IoStyle::NamNabm, 2, false},
        {pci_vendor_ati, pci_dev_ixp200, "ATI IXP200",
         Ac97IoStyle::DirectRegs, 2, false},
        {pci_vendor_ati, pci_dev_ixp400, "ATI IXP400/SB450",
         Ac97IoStyle::DirectRegs, 2, true},
        {pci_vendor_ali, pci_dev_ali5455, "ALi M5455",
         Ac97IoStyle::NamNabm, 2, false},
        {pci_vendor_trident, pci_dev_trident_4dwave_nx, "Trident 4DWave-NX",
         Ac97IoStyle::NamNabm, 2, false},
    };
    constexpr hcpp::size_t ac97_pci_table_count =
        sizeof(ac97_pci_table) / sizeof(ac97_pci_table[0]);

    inline const Ac97PciId *ac97_pci_lookup(hcpp::u16 vendor,
                                            hcpp::u16 device) noexcept {
        for (hcpp::size_t i = 0; i < ac97_pci_table_count; ++i) {
            if (ac97_pci_table[i].vendor == vendor &&
                ac97_pci_table[i].device == device) {
                return &ac97_pci_table[i];
            }
        }
        return nullptr;
    }

    // Resolve the effective BAR pair for a table row + raw BARs.
    inline void ac97_bars(const Ac97PciId &id, const hcpp::u32 bars[6],
                          hcpp::u32 &nambar, hcpp::u32 &nabmbar) noexcept {
        switch (id.style) {
        case Ac97IoStyle::NamNabm:
            nambar = bars[0];
            nabmbar = bars[1];
            break;
        case Ac97IoStyle::SingleNabm:
            nambar = bars[0];
            nabmbar = bars[0] + 0x80;
            break;
        case Ac97IoStyle::DirectRegs:
        default:
            nambar = bars[0];
            nabmbar = bars[0];
            break;
        }
    }

}  // namespace hype::chips::ac97
