#pragma once

// Yamaha YMF7x4 (YMF724/724F/740/740C/744/754) PCI audio (issue #118,
// wave 2).  Register map mirrors ALSA sound/pci/ymfpci/ymfpci_main.c.
//
// BAR0 is 0x8000 bytes of MMIO.  The DS-1E engine runs two firmware
// blobs (ds1e_ctrl.fw control microcode + ds1e_dsp.fw DSP image)
// uploaded through DSPINSTRAM at 0x1000; native playback is gated on
// those.  The AC'97 front end works without firmware and is exercised
// directly for codec presence and mixer writes.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::ymfpci {

    constexpr hcpp::u16 vendor_yamaha = 0x1073;
    // YMF724 through YMF754 device ids.
    constexpr hcpp::u16 device_ids[] = {0x0004, 0x000D, 0x0005, 0x0006,
                                        0x0008, 0x0009, 0x000C, 0x0010,
                                        0x0012};
    constexpr hcpp::size_t device_id_count = 9;

    constexpr hcpp::u32 io_extent = 0x8000;

    // MMIO registers (byte offsets into BAR0).
    constexpr hcpp::u32 status = 0x0004;
    constexpr hcpp::u32 global_ctrl = 0x0008;
    constexpr hcpp::u32 pci_int_status = 0x0010;
    constexpr hcpp::u32 pci_int_en = 0x0014;
    constexpr hcpp::u32 lg_ext_int = 0x004E;
    constexpr hcpp::u32 ac97_cmd_data = 0x0060;
    constexpr hcpp::u32 ac97_cmd_adr = 0x0062;
    constexpr hcpp::u32 status_data = 0x0064;
    constexpr hcpp::u32 status_adr = 0x0066;
    constexpr hcpp::u32 ac97_psec_vdown = 0x006A;
    constexpr hcpp::u32 ac97_psec_vup = 0x006C;
    constexpr hcpp::u32 ac97_ssec_vdown = 0x006E;
    constexpr hcpp::u32 ac97_ssec_vup = 0x0070;
    constexpr hcpp::u32 ac97_modem_ring = 0x0072;
    constexpr hcpp::u32 timer_count = 0x0100;
    constexpr hcpp::u32 mode = 0x0108;
    constexpr hcpp::u32 config = 0x0114;
    constexpr hcpp::u32 play_ctrl_size = 0x0140;
    constexpr hcpp::u32 rec_ctrl_size = 0x0144;
    constexpr hcpp::u32 eff_ctrl_size = 0x0148;
    constexpr hcpp::u32 work_size = 0x014C;
    constexpr hcpp::u32 map_of_rec = 0x0150;
    constexpr hcpp::u32 map_of_eff = 0x0154;
    constexpr hcpp::u32 play_ctrl_base = 0x0158;
    constexpr hcpp::u32 rec_ctrl_base = 0x015C;
    constexpr hcpp::u32 eff_ctrl_base = 0x0160;
    constexpr hcpp::u32 work_base = 0x0164;
    constexpr hcpp::u32 dsp_instram = 0x1000;
    constexpr hcpp::u32 ac97_regs = 0x0800;

    // YMFPCII_MODE bits.
    constexpr hcpp::u32 mode_dsp_run = 0x00000003;
    constexpr hcpp::u32 mode_i2c_eaddr = 0x00000004;

    // YMFPCII_CONFIG bits.
    constexpr hcpp::u32 config_dsp_on = 0x00000001;

    // AC97 secondary status: bit15 of STATUS_ADR clear = codec ready.
    constexpr hcpp::u32 status_ready_mask = 0x8000;

    // AC97 mixer offsets used at init (register *2).
    constexpr hcpp::u16 ac97_master = 0x02 * 2;
    constexpr hcpp::u16 ac97_pcm_out = 0x18 * 2;
    constexpr hcpp::u16 ac97_nativedac_out = 0x84 * 2;  // YMF only
    constexpr hcpp::u16 ac97_codec_id0 = 0x7C * 2;
    constexpr hcpp::u16 ac97_codec_id1 = 0x7E * 2;

    // PCI config register: DSXG_CTRL AC-link reset.
    constexpr hcpp::u16 pcir_dsxg_ctrl = 0x48;

    constexpr hcpp::u32 pcm_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;
    constexpr hcpp::u8 ring_pages = 2;

    struct YmfpciController {
        hcpp::uintptr_t mmio;
        bool reset_done;
        bool codec_ready;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 buffer_frames;
        bool fw_loaded;
        bool dsp_running;
    };

    const pci::PciDevice *ymfpci_find() noexcept;
    bool ymfpci_bind(const pci::PciDevice &device,
                     YmfpciController &controller) noexcept;
    bool ymfpci_reset(YmfpciController &controller,
                      const pci::PciDevice &device) noexcept;
    bool ymfpci_codec_ready(YmfpciController &controller) noexcept;
    bool ymfpci_ac97_read(const YmfpciController &controller, hcpp::u16 reg,
                          hcpp::u16 &value) noexcept;
    bool ymfpci_ac97_write(YmfpciController &controller, hcpp::u16 reg,
                           hcpp::u16 value) noexcept;
    bool ymfpci_load_firmware(YmfpciController &controller,
                              const void *ctrl, hcpp::u32 ctrl_bytes,
                              const void *dsp, hcpp::u32 dsp_bytes) noexcept;
    bool ymfpci_play(YmfpciController &controller) noexcept;
    void ymfpci_stop(YmfpciController &controller) noexcept;
    hcpp::u32 ymfpci_pos(const YmfpciController &controller) noexcept;
    bool ymfpci_present() noexcept;
    bool ymfpci_selftest() noexcept;

}  // namespace hype::audio::ymfpci
