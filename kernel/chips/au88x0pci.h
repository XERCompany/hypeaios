#pragma once

// Aureal Vortex PCI glue: vendor/device IDs, per-model class tags and
// the enable/shutdown bit patterns drivers program into VORTEX_CTRL
// and the codec-enable register during probe and teardown.
//
// Reference: Linux sound/pci/au88x0/au88x0.c pci table and the
// vortex_core_init()/vortex_core_shutdown() sequences in
// au88x0_core.c.

#include "hcpp/types.hpp"

namespace hype::chips::au88x0 {

    // ---- PCI identities --------------------------------------------------
    constexpr hcpp::u16 pci_vendor_aureal = 0x12EB;
    constexpr hcpp::u16 pci_dev_vortex_1 = 0x0001;    // AU8820
    constexpr hcpp::u16 pci_dev_vortex_2 = 0x0002;    // AU8830
    constexpr hcpp::u16 pci_dev_advantage = 0x0003;   // AU8810

    // Multimedia audio class.
    constexpr hcpp::u8 pci_class_multimedia = 0x04;
    constexpr hcpp::u8 pci_subclass_audio = 0x01;

    // Silicon revision that reports 0xFE and misbehaves; quirked.
    constexpr hcpp::u8 rev_bad = 0xFE;

    // MMIO BAR carrying the register window (a-au88xx.h offsets).
    constexpr hcpp::u8 bar_mmio = 0;

    enum class Au88x0Model : hcpp::u8 {
        Au8810,
        Au8820,
        Au8830,
        Unknown = 0xFF,
    };

    struct Au88x0PciId {
        hcpp::u16 device;
        Au88x0Model model;
        const char *name;
        bool has_eq;      // hardware parametric EQ (8810, 8830)
        bool has_wt;      // wavetable engine (8820, 8830)
        bool has_a3d;     // A3D/HRTF slices (8810, 8830)
        bool has_spdif;
        bool has_modem;   // AC'98 modem interface (8830)
    };

    inline constexpr Au88x0PciId au88x0_pci_table[] = {
        {pci_dev_advantage, Au88x0Model::Au8810, "Advantage AU8810",
         true, false, true, false, false},
        {pci_dev_vortex_1, Au88x0Model::Au8820, "Vortex AU8820",
         false, true, false, true, false},
        {pci_dev_vortex_2, Au88x0Model::Au8830, "Vortex 2 AU8830",
         true, true, true, true, true},
    };
    constexpr hcpp::size_t au88x0_pci_table_count =
        sizeof(au88x0_pci_table) / sizeof(au88x0_pci_table[0]);

    inline const Au88x0PciId *au88x0_pci_lookup(
        hcpp::u16 vendor, hcpp::u16 device) noexcept {
        if (vendor != pci_vendor_aureal) return nullptr;
        for (hcpp::size_t i = 0; i < au88x0_pci_table_count; ++i) {
            if (au88x0_pci_table[i].device == device) {
                return &au88x0_pci_table[i];
            }
        }
        return nullptr;
    }

    // rev 0xFE on Vortex2/Advantage is the documented bad-silicon case
    // (Linux IS_BAD_CHIP()).
    inline bool au88x0_is_bad_chip(hcpp::u16 device, hcpp::u8 rev) noexcept {
        return rev == rev_bad &&
               (device == pci_dev_vortex_2 || device == pci_dev_advantage);
    }

    // Probe-time codec enable masks (VORTEX_CODEC_EN): the union that
    // brings up the audio codec interface on each model.
    constexpr hcpp::u32 codec_en_8820 = 0x00000300;
    constexpr hcpp::u32 codec_en_8810 = 0x00000300 | 0x00003000;
    constexpr hcpp::u32 codec_en_8830 = 0x00000300 | 0x00003000;

}  // namespace hype::chips::au88x0
