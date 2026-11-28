#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Digigram LoLa PCI-e board (1369:0001).
// Register map mirrors Linux ALSA sound/pci/lola/lola.h: BAR0 is
// an HDA-style controller file (GCTL/CORB/RIRB/stream rings) and
// BAR1 carries the FPGA/board registers plus the DSD stream
// descriptors. Codec "widgets" are reached through CORB/RIRB verbs
// exactly like HDA but with LoLa-specific node ids.
//
// Working: identify, BAR0+BAR1 claim, GCTL reset, CORB/RIRB ring
// setup (one PMM page, CORB at 0 / RIRB at 2048), VENDOR_ID verb
// roundtrip verifying the 0x1369 codec vendor.
// Offline: stream programming (DSD descriptors in BAR1) and the
// widget-tree walk — engine_ready stays false.

namespace hype::audio::lola {

    constexpr hcpp::u16 vendor_digigram = 0x1369;
    constexpr hcpp::u16 device_lola = 0x0001;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // BAR0 register offsets (lola.h).
    constexpr hcpp::u32 bar0_gcap = 0x00;
    constexpr hcpp::u32 bar0_vmin = 0x02;
    constexpr hcpp::u32 bar0_vmaj = 0x03;
    constexpr hcpp::u32 bar0_gctl = 0x08;
    constexpr hcpp::u32 bar0_statests = 0x0e;
    constexpr hcpp::u32 bar0_gsts = 0x10;
    constexpr hcpp::u32 bar0_intctl = 0x20;
    constexpr hcpp::u32 bar0_intsts = 0x24;
    constexpr hcpp::u32 bar0_walclk = 0x30;
    constexpr hcpp::u32 bar0_corblbase = 0x40;
    constexpr hcpp::u32 bar0_corbubase = 0x44;
    constexpr hcpp::u32 bar0_corbwp = 0x48;
    constexpr hcpp::u32 bar0_corbrp = 0x4a;
    constexpr hcpp::u32 bar0_corbctl = 0x4c;
    constexpr hcpp::u32 bar0_corbsts = 0x4d;
    constexpr hcpp::u32 bar0_corbsize = 0x4e;
    constexpr hcpp::u32 bar0_rirblbase = 0x50;
    constexpr hcpp::u32 bar0_rirbubase = 0x54;
    constexpr hcpp::u32 bar0_rirbwp = 0x58;
    constexpr hcpp::u32 bar0_rintcnt = 0x5a;
    constexpr hcpp::u32 bar0_rirbctl = 0x5c;
    constexpr hcpp::u32 bar0_rirbsts = 0x5d;
    constexpr hcpp::u32 bar0_rirbsize = 0x5e;
    constexpr hcpp::u32 bar0_dplbase = 0x70;
    constexpr hcpp::u32 bar0_dpubase = 0x74;

    // BAR1 register offsets (lola.h).
    constexpr hcpp::u32 bar1_fpgaver = 0x00;
    constexpr hcpp::u32 bar1_dever = 0x04;
    constexpr hcpp::u32 bar1_board_ctrl = 0x0f00;
    constexpr hcpp::u32 bar1_board_mode = 0x0f02;
    constexpr hcpp::u32 bar1_dintctl = 0x80;

    constexpr hcpp::u32 gctl_reset = 0x00000001;
    constexpr hcpp::u32 gctl_uren = 0x00000100;
    constexpr hcpp::u8 rbctl_dma_en = 0x02;
    constexpr hcpp::u8 rbctl_irq_en = 0x01;
    constexpr hcpp::u16 rbrwp_clr = 0x8000;
    constexpr hcpp::u8 ring_size_256 = 0x02;    // 256 entries
    constexpr hcpp::u16 corb_entries = 256;
    constexpr hcpp::u32 rirb_offset = 2048;     // corb 0..2047 in page

    // Verbs: word = (nid << 20) | (verb << 8) | param8 for 8-bit
    // params; LOLA_PAR_VENDOR_ID = 0x00 on the AFG node 0.
    constexpr hcpp::u8 par_vendor_id = 0x00;
    constexpr hcpp::u32 verb_get_parameter = 0xf00;
    constexpr hcpp::u32 expected_vendor = 0x1369;

    constexpr hcpp::u8 ring_pages = 4;

    struct LolaController {
        hcpp::uintptr_t bar0;
        hcpp::uintptr_t bar1;
        bool reset_done;
        bool engine_ready;
        hcpp::u32 rb_phys;
        volatile hcpp::u32 *corb;
        volatile hcpp::u32 *rirb;
        hcpp::u16 corb_wp;
        hcpp::u16 rirb_rp;
        hcpp::u32 codec_vendor;
        hcpp::u32 board_version;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *lola_find() noexcept;
    bool lola_bind(const pci::PciDevice &device,
                   LolaController &controller) noexcept;
    bool lola_reset(LolaController &controller) noexcept;
    bool lola_present() noexcept;
    bool lola_probe() noexcept;

}  // namespace hype::audio::lola
