#pragma once

// Digigram miXart / miXart8 PCI audio board (issue #118, wave 2).
// Register offsets mirror ALSA sound/pci/mixart/mixart_hwdep.h.
//
// The miXart board exposes its on-board MPC8240 PowerPC host bridge as
// the PCI function, so ALSA snd-mixart binds 1057:0003.  The issue text
// mentioned Digigram 1369:0005; Digigram's vendor id is real, but the
// 0005 device id could not be verified against ALSA, so it is kept as a
// secondary (uncertain) match.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::mixart {

    constexpr hcpp::u16 vendor_motorola = 0x1057;
    constexpr hcpp::u16 device_mpc8240 = 0x0003;
    constexpr hcpp::u16 vendor_digigram = 0x1369;
    constexpr hcpp::u16 device_mixart8 = 0x0005;  // unverified alternate

    // BAR0: 16 MiB memory window holding sample RAM, pseudoregisters,
    //       mailbox stacks and message frames.
    // BAR1: 4 KiB MPC8240 bridge register window.
    constexpr hcpp::u32 ba0_size = 16u * 1024u * 1024u;
    constexpr hcpp::u32 ba1_size = 4u * 1024u;

    // BAR1 registers (little-endian 32-bit).
    constexpr hcpp::u32 reg_omisr = 0x30;         // outbound msg irq status
    constexpr hcpp::u32 reg_omimr = 0x34;         // outbound msg irq mask
    constexpr hcpp::u32 reg_odbr = 0x60;          // outbound doorbell
    constexpr hcpp::u32 reg_brutal_reset = 0x68;  // write 1 -> board reset

    constexpr hcpp::u32 omimr_mask_all = 0x02B;
    constexpr hcpp::u32 omimr_allow_odbr = 0x023;
    constexpr hcpp::u32 omisr_oidi = 0x008;

    // BAR0 pseudoregisters (big-endian 32-bit).
    constexpr hcpp::u32 pseudoreg = 0x2000;
    constexpr hcpp::u32 pr_boardnumber = pseudoreg + 0x00;
    constexpr hcpp::u32 pr_mxlx_base_addr = pseudoreg + 0x9C;
    constexpr hcpp::u32 pr_mxlx_size = pseudoreg + 0xA0;
    constexpr hcpp::u32 pr_mxlx_status = pseudoreg + 0xA4;
    constexpr hcpp::u32 pr_elf_status = pseudoreg + 0xB0;
    constexpr hcpp::u32 pr_dbrd_presence = pseudoreg + 0x990;
    constexpr hcpp::u32 pr_dbrd_type = pseudoreg + 0x994;
    constexpr hcpp::u32 pr_flowtable_ptr = 0x3000;

    // Message frame region (little-endian).
    constexpr hcpp::u32 msg_frame_offset = 0x64;
    constexpr hcpp::u32 msg_frame_size = 0x6400;
    constexpr hcpp::u32 msg_frame_number = 32;
    constexpr hcpp::u32 msg_from_agent_itmf =
        msg_frame_offset + msg_frame_size * msg_frame_number;
    constexpr hcpp::u32 msg_to_agent_itmf = msg_from_agent_itmf + msg_frame_size;
    constexpr hcpp::u32 msg_host_rsc_protection = msg_to_agent_itmf + msg_frame_size;
    constexpr hcpp::u32 msg_agent_rsc_protection = msg_host_rsc_protection + 4;

    // Where the motherboard Xilinx image is dropped inside BAR0.
    constexpr hcpp::u32 mxlx_base_address = 0x00600000;

    // Embedded firmware load status values (EMBEBBED_STAT_*).
    constexpr hcpp::u32 stat_xfer_wait = 1;   // waiting for image upload
    constexpr hcpp::u32 stat_xfer_done = 2;   // host completed upload
    constexpr hcpp::u32 stat_board_reset = 5; // board performing a reset

    struct MixartController {
        hcpp::uintptr_t mem0;   // BAR0 16 MiB window
        hcpp::uintptr_t regs;   // BAR1 4 KiB register window
        bool reset_done;
        hcpp::u32 fw_bytes;
        bool fw_loaded;
        hcpp::u32 mxlx_status;
    };

    const pci::PciDevice *mixart_find() noexcept;
    bool mixart_bind(const pci::PciDevice &device,
                     MixartController &controller) noexcept;
    bool mixart_reset(MixartController &controller) noexcept;
    bool mixart_firmware(MixartController &controller, const void *image,
                         hcpp::u32 bytes) noexcept;
    bool mixart_present() noexcept;
    bool mixart_selftest() noexcept;

}  // namespace hype::audio::mixart
