#pragma once

// Digigram VX222 PCI audio board (issue #118, wave 2).
//
// The VX222 hardware enumerates as a PLX bus bridge, not under the
// Digigram vendor id: OLD cards use 0x10B5:0x9050 (PLX9050), NEW cards
// 0x10B5:0x9030 (PLX9030), both carrying a Digigram subsystem vendor
// 0x1369.  The IDs listed in the issue text are the subsystem ids, not
// the vendor/device pair.
//
// Like the rest of the Digigram family the card runs DSP firmware that
// is uploaded at probe time (upstream loads vx222.bin via the hwdep
// loader); without it the card stays inert -> firmware path.
//
// The PLX chip provides a generic mailbox + Xilinx serial-load
// interface in its local I/O BAR; the DSP itself sits behind a second
// I/O BAR.  Registers below mirror ALSA sound/pci/vx222/vx222_hwdep.c
// plus the shared PLX control block layout.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::vx222 {

    constexpr hcpp::u16 vendor_plx = 0x10B5;
    constexpr hcpp::u16 device_plx9030 = 0x9030;  // "new" revision
    constexpr hcpp::u16 device_plx9050 = 0x9050;  // "old" revision
    constexpr hcpp::u16 subvendor_digigram = 0x1369;

    // PLX9030/9050 local-space registers (offsets into the I/O BAR).
    constexpr hcpp::u16 plx_reset = 0x28;
    constexpr hcpp::u16 plx_mbox0 = 0x40;
    constexpr hcpp::u16 plx_mbox1 = 0x44;
    constexpr hcpp::u16 plx_irqcs = 0x4C;
    constexpr hcpp::u16 plx_chipsc = 0x6C;

    // INTCSR bits.
    constexpr hcpp::u32 irqcs_enable_pciirq = 0x0040;
    constexpr hcpp::u32 irqcs_enable_pcidb = 0x0200;

    // MailBox flags used by the vx core.
    constexpr hcpp::u32 mbox_boot_command = 0x00000100;
    constexpr hcpp::u32 mbox_boot_here = 0x00000001;

    // CNTRL bits (serial Xilinx download on the 9030/9050).
    constexpr hcpp::u32 cntrl_serial_init = 0x00100000;
    constexpr hcpp::u32 cntrl_reset_dsp = 0x10000000;
    constexpr hcpp::u32 cntrl_hold_dsp = 0x40000000;
    constexpr hcpp::u32 cntrl_m0 = 0x00001000;
    constexpr hcpp::u32 cntrl_m1 = 0x00002000;
    constexpr hcpp::u32 cntrl_m2 = 0x00004000;
    constexpr hcpp::u32 cntrl_tcx = 0x00008000;

    // Xilinx-program side bits of the same CNTRL dword.
    constexpr hcpp::u32 xilinx_prog = 0x00000100;
    constexpr hcpp::u32 xilinx_init = 0x00000200;
    constexpr hcpp::u32 xilinx_data_clk = 0x00000400;
    constexpr hcpp::u32 xilinx_data_in = 0x00000800;

    // DSP register file inside the second I/O BAR (VX-family layout).
    constexpr hcpp::u16 dsp_csr = 0x00;    // control/status (32-bit)
    constexpr hcpp::u16 dsp_iswr = 0x04;   // internal status word reg
    constexpr hcpp::u16 dsp_icvr = 0x08;   // internal control value
    constexpr hcpp::u16 dsp_obe = 0x0C;    // output buffer empty flag
    constexpr hcpp::u16 dsp_ibf = 0x10;    // input buffer full flag
    constexpr hcpp::u16 dsp_out_fifo = 0x14;  // outbound msg fifo
    constexpr hcpp::u16 dsp_in_fifo = 0x18;   // inbound msg fifo

    // ICVR / CSR bits.
    constexpr hcpp::u32 icvr_armrq = 0x00000001;
    constexpr hcpp::u32 csr_run = 0x00000001;
    constexpr hcpp::u32 csr_reset = 0x00000002;

    constexpr hcpp::u32 poll_tries = 20000;

    enum class Vx222Revision : hcpp::u8 { Old, New };

    struct Vx222Controller {
        hcpp::u16 plx;   // PLX local I/O BAR
        hcpp::u16 dsp;   // DSP register BAR
        Vx222Revision revision;
        bool xilinx_done;
        bool boot_here;
        bool reset_done;
        bool fw_loaded;
    };

    const pci::PciDevice *vx222_find() noexcept;
    bool vx222_bind(const pci::PciDevice &device,
                    Vx222Controller &controller) noexcept;
    bool vx222_reset(Vx222Controller &controller) noexcept;
    bool vx222_load_xilinx(Vx222Controller &controller, const void *image,
                           hcpp::u32 bytes) noexcept;
    bool vx222_present() noexcept;
    bool vx222_selftest() noexcept;

}  // namespace hype::audio::vx222
