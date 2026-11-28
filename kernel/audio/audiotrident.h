#pragma once

// Trident 4DWave-DX / 4DWave-NX PCI audio (issue #118, wave 2); the
// ALSA trident driver also binds the SiS 7018 and ALi M5451 clones.
// Register map mirrors ALSA sound/pci/trident/trident.h and
// trident_main.c.
//
// BAR0 is an I/O space.  The engine has 64 hardware voices in two banks
// (A = 0-31, B = 32-63); a voice is selected by writing its number to
// T4D_LFO_GC_CIR and is fully programmed with five dwords at CH_START.
// On DX the LBA field is a direct physical address; on NX it is an
// offset into a 16 MiB TLB window whose page table lives in host RAM
// (NX_TLBC points at it, bit0 enables translation).

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::trident {

    constexpr hcpp::u16 vendor_trident = 0x1023;
    constexpr hcpp::u16 device_4dwave_dx = 0x2000;
    constexpr hcpp::u16 device_4dwave_nx = 0x2001;
    constexpr hcpp::u16 vendor_sis = 0x1039;
    constexpr hcpp::u16 device_sis7018 = 0x7018;
    constexpr hcpp::u16 vendor_ali = 0x10B9;
    constexpr hcpp::u16 device_ali5451 = 0x5451;

    // Global / bank registers (I/O offsets).
    constexpr hcpp::u16 t4d_start_a = 0x80;
    constexpr hcpp::u16 t4d_stop_a = 0x84;
    constexpr hcpp::u16 t4d_dly_a = 0x88;
    constexpr hcpp::u16 t4d_aint_a = 0x98;
    constexpr hcpp::u16 t4d_ainten_a = 0x9C;
    constexpr hcpp::u16 t4d_lfo_gc_cir = 0xA0;
    constexpr hcpp::u16 t4d_musicvol_wavevol = 0xA8;
    constexpr hcpp::u16 t4d_miscint = 0xB0;
    constexpr hcpp::u16 t4d_start_b = 0xB4;
    constexpr hcpp::u16 t4d_stop_b = 0xB8;
    constexpr hcpp::u16 t4d_aint_b = 0xD8;
    constexpr hcpp::u16 t4d_ainten_b = 0xDC;
    constexpr hcpp::u16 t4d_rci = 0x70;
    constexpr hcpp::u16 nx_tlbc = 0x6C;
    constexpr hcpp::u32 nx_sb_irq_disable = 0x00001000;

    // Channel registers (relative to the selected voice).
    constexpr hcpp::u16 ch_start = 0xE0;
    constexpr hcpp::u16 ch_dx_cso_alpha_fms = 0xE0;
    constexpr hcpp::u16 ch_dx_eso_delta = 0xE8;
    constexpr hcpp::u16 ch_dx_fmc_rvol_cvol = 0xEC;
    constexpr hcpp::u16 ch_nx_delta_cso = 0xE0;
    constexpr hcpp::u16 ch_nx_delta_eso = 0xE8;
    constexpr hcpp::u16 ch_nx_alpha_fms_fmc_rvol_cvol = 0xEC;
    constexpr hcpp::u16 ch_lba = 0xE4;
    constexpr hcpp::u16 ch_gvsel_pan_vol_ctrl_ec = 0xF0;

    // AC'97 registers.
    constexpr hcpp::u16 dx_acr0_ac97_w = 0x40;
    constexpr hcpp::u16 dx_acr1_ac97_r = 0x44;
    constexpr hcpp::u16 dx_acr2_ac97_com_stat = 0x48;
    constexpr hcpp::u16 nx_acr0_ac97_com_stat = 0x40;
    constexpr hcpp::u16 nx_acr1_ac97_w = 0x44;
    constexpr hcpp::u16 nx_acr2_ac97_r_primary = 0x48;
    constexpr hcpp::u16 nx_acr3_ac97_r_secondary = 0x4C;

    constexpr hcpp::u32 dx_ac97_busy_write = 0x8000;
    constexpr hcpp::u32 dx_ac97_busy_read = 0x8000;
    constexpr hcpp::u32 dx_ac97_ready = 0x0010;
    constexpr hcpp::u32 nx_ac97_busy_write = 0x0800;
    constexpr hcpp::u32 nx_ac97_busy_read = 0x0800;
    constexpr hcpp::u32 nx_ac97_primary_ready = 0x0008;

    // AC'97 mixer registers used at init.
    constexpr hcpp::u8 ac97_master = 0x02;
    constexpr hcpp::u8 ac97_pcm_out = 0x18;

    // TLB parameters (NX): 4096 entries of 4 KiB pages, 16 KiB-aligned.
    constexpr hcpp::u32 tlb_entries = 4096;
    constexpr hcpp::u32 tlb_bytes = tlb_entries * 4;

    constexpr hcpp::u32 pcm_rate = 44100;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;
    constexpr hcpp::u8 ring_pages = 4;

    enum class TridentKind : hcpp::u8 { Dx, Nx, Sis7018, Ali5451 };

    struct TridentController {
        hcpp::u16 iobase;
        TridentKind kind;
        hcpp::u32 ac97_ctrl;
        bool reset_done;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 buffer_frames;
        hcpp::u8 voice;
        bool running;
    };

    const pci::PciDevice *trident_find() noexcept;
    bool trident_bind(const pci::PciDevice &device,
                      TridentController &controller) noexcept;
    bool trident_reset(TridentController &controller) noexcept;
    bool trident_play(TridentController &controller) noexcept;
    void trident_stop(TridentController &controller) noexcept;
    hcpp::u32 trident_pos(TridentController &controller) noexcept;
    bool trident_present() noexcept;
    bool trident_selftest() noexcept;

}  // namespace hype::audio::trident
