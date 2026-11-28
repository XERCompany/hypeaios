#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Creative X-Fi — EMU20k1 (SB X-Fi) and EMU20k2 (X-Fi Titanium).
// Register maps mirror Linux ALSA sound/pci/ctxfi:
//  - 20k1: BAR0 is the flat register window (ct20k1reg.h),
//  - 20k2: BAR2 is the register window (ct20k2reg.h).
//
// Working: identify, register-window claim, PLL init, DSP
// auto-init kick (GCTL AIE->AID handshake), audio-ring enable
// bits, interrupt quiesce, GPIO default config.
// Offline: the virtual-memory transport (VMEM_PTPx page tables +
// TRNCTL) and SRC/AMPTP resource programming — engine stays off.
// Sound Core3D parts (1102:0012/0021, Recon3D/Z-series) are NOT
// ctxfi devices — they are HDA-class codecs handled by the HDA
// path, so they are deliberately not claimed here.

namespace hype::audio::ctxfi {

    constexpr hcpp::u16 vendor_creative = 0x1102;
    constexpr hcpp::u16 device_20k1 = 0x0005;   // EMU20k1
    constexpr hcpp::u16 device_20k2 = 0x000b;   // EMU20k2

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // 20k1 register offsets (BAR0), ct20k1reg.h.
    constexpr hcpp::u32 r1_trnctl = 0x13b404;   // transport control
    constexpr hcpp::u32 r1_srcip = 0x1b102c;    // SRC int pending
    constexpr hcpp::u32 r1_gie = 0x1c6014;      // global int enable
    constexpr hcpp::u32 r1_gpioctl = 0x1c6024;  // GPIO config
    constexpr hcpp::u32 r1_pllctl = 0x1c6060;   // PLL control
    constexpr hcpp::u32 r1_gctl = 0x1c6070;     // global control

    // 20k1 GCTL bits (cthw20k1.c).
    constexpr hcpp::u32 r1_gctl_eac = 0x00000001;
    constexpr hcpp::u32 r1_gctl_eai = 0x00000002;   // auto-init kick
    constexpr hcpp::u32 r1_gctl_dbp = 0x00000020;
    constexpr hcpp::u32 r1_gctl_tbp = 0x00000080;
    constexpr hcpp::u32 r1_gctl_fbp = 0x00000200;
    constexpr hcpp::u32 r1_gctl_et = 0x00000800;
    constexpr hcpp::u32 r1_gctl_aid = 0x00100000;   // auto-init done

    constexpr hcpp::u32 r1_pll_48k = 0x1480a001;    // 48 kHz ref
    constexpr hcpp::u32 r1_pll_96k = 0x1480a731;    // 96 kHz ref

    // 20k2 register offsets (BAR2), ct20k2reg.h.
    constexpr hcpp::u32 r2_transport_enb = 0x1b6000;
    constexpr hcpp::u32 r2_transport_ctl = 0x1b6004;
    constexpr hcpp::u32 r2_gip = 0x1b7010;      // global int pending
    constexpr hcpp::u32 r2_gie = 0x1b7014;      // global int enable
    constexpr hcpp::u32 r2_gpio_ctrl = 0x1b7024;
    constexpr hcpp::u32 r2_pll_ctl = 0x1b7080;
    constexpr hcpp::u32 r2_pll_stat = 0x1b7084;
    constexpr hcpp::u32 r2_pll_enb = 0x1b7088;
    constexpr hcpp::u32 r2_gctl = 0x1b7090;     // GLOBAL_CNTL_GCTL
    constexpr hcpp::u32 r2_src_ip = 0x1a102c;
    constexpr hcpp::u32 r2_mixer_ar_enable = 0x19000c;
    constexpr hcpp::u32 r2_vmem_ptpal = 0x1c6300;
    constexpr hcpp::u32 r2_vmem_ptpah = 0x1c6304;
    constexpr hcpp::u32 r2_vmem_ctl = 0x1c7000;

    // 20k2 GCTL bits (cthw20k2.c).
    constexpr hcpp::u32 r2_gctl_aie = 0x00000001;   // auto-init kick
    constexpr hcpp::u32 r2_gctl_uaa = 0x00000002;   // UAA fallback mode
    constexpr hcpp::u32 r2_gctl_dpc = 0x00000004;
    constexpr hcpp::u32 r2_gctl_dbp = 0x00000008;
    constexpr hcpp::u32 r2_gctl_tbp = 0x00000020;
    constexpr hcpp::u32 r2_gctl_fbp = 0x00000080;
    constexpr hcpp::u32 r2_gctl_aid = 0x00001000;   // auto-init done

    // 20k2 PLL two-step values for 48 kHz reference (cthw20k2.c):
    // base 0x20C00000 | FD=(16-4)<<8 | RD=(1-1)<<4, then FD=(16-2)<<8.
    constexpr hcpp::u32 r2_pll_step1 = 0x20c00c00;
    constexpr hcpp::u32 r2_pll_step2 = 0x20c00e00;
    constexpr hcpp::u32 r2_pll_enb_all = 0x0000000b;
    constexpr hcpp::u32 r2_pllstat_pd = 0x00100000;  // powered-down flag

    struct CtxfiController {
        hcpp::uintptr_t mmio;
        hcpp::u16 device_id;
        bool is20k2;
        bool reset_done;
        bool engine_ready;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *ctxfi_find() noexcept;
    bool ctxfi_bind(const pci::PciDevice &device,
                    CtxfiController &controller) noexcept;
    bool ctxfi_reset(CtxfiController &controller) noexcept;
    bool ctxfi_present() noexcept;
    bool ctxfi_probe() noexcept;

}  // namespace hype::audio::ctxfi
