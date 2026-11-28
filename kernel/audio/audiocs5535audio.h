#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// NS Geode CS5535 / AMD CS5536 companion-chip AC'97 audio.
// Register map mirrors Linux ALSA sound/pci/cs5535audio
// (cs5535audio.h): single I/O BAR0 with the XpressAUDIO flat
// register file — ACC_CODEC_CNTL/ACC_CODEC_STATUS carry the codec
// command protocol, BM0/BM1 are the two busmaster channels with
// classic PRD tables.
//
// Working: identify, BAR0 claim, codec read/write handshake, codec
// reset + vendor id + master volume, BM0 PRD table programmed.
// Offline: actual DMA start (BM_CTL_EN write) is left out — there
// is no IRQ plumbing yet, so the channel stays armed not running.

namespace hype::audio::cs5535audio {

    constexpr hcpp::u16 vendor_ns = 0x100b;
    constexpr hcpp::u16 device_cs5535_audio = 0x002e;  // ALSA NS id
    constexpr hcpp::u16 vendor_amd = 0x1022;
    constexpr hcpp::u16 device_cs5536_audio = 0x2093;  // ALSA AMD id
    // Note: the issue sketch suggested 1078:0103; the ALSA
    // snd-cs5535audio pci_device_id table uses 100b:002e and
    // 1022:2093, which are the ids implemented here.

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // I/O register offsets (BAR0).
    constexpr hcpp::u8 acc_gpio_status = 0x00;
    constexpr hcpp::u8 acc_codec_status = 0x08;
    constexpr hcpp::u8 acc_codec_cntl = 0x0c;
    constexpr hcpp::u8 acc_irq_status = 0x12;
    constexpr hcpp::u8 acc_bm0_cmd = 0x20;
    constexpr hcpp::u8 acc_bm0_status = 0x21;
    constexpr hcpp::u8 acc_bm0_prd = 0x24;
    constexpr hcpp::u8 acc_bm1_cmd = 0x28;
    constexpr hcpp::u8 acc_bm1_status = 0x29;
    constexpr hcpp::u8 acc_bm1_prd = 0x2c;
    constexpr hcpp::u8 acc_bm0_pntr = 0x60;
    constexpr hcpp::u8 acc_bm1_pntr = 0x64;

    // Codec command protocol bits.
    constexpr hcpp::u32 cmd_mask = 0xff00ffff;
    constexpr hcpp::u32 cmd_new = 0x00010000;
    constexpr hcpp::u32 sts_new = 0x00020000;
    constexpr hcpp::u32 prm_rdy_sts = 0x00800000;
    constexpr hcpp::u32 codec_cntl_wr_cmd = 0x00000000;  // bit31 clear
    constexpr hcpp::u32 codec_cntl_rd_cmd = 0x80000000;
    constexpr hcpp::u32 codec_cntl_lnk_shutdown = 0x00040000;
    constexpr hcpp::u32 codec_cntl_lnk_wrm_rst = 0x00020000;

    // Busmaster command/status bits.
    constexpr hcpp::u8 bm_ctl_en = 0x01;
    constexpr hcpp::u8 bm_ctl_pause = 0x03;
    constexpr hcpp::u8 bm_ctl_dis = 0x00;
    constexpr hcpp::u8 bm_eop = 0x01;
    constexpr hcpp::u8 bm_eop_err = 0x02;

    // PRD flags (upper 16 bits of descriptor word 1).
    constexpr hcpp::u16 prd_jmp = 0x2000;
    constexpr hcpp::u16 prd_eop = 0x4000;
    constexpr hcpp::u16 prd_eot = 0x8000;

    // AC'97 codec registers.
    constexpr hcpp::u8 ac97_reset = 0x00;
    constexpr hcpp::u8 ac97_master_volume = 0x02;
    constexpr hcpp::u8 ac97_pcmout_volume = 0x18;
    constexpr hcpp::u8 ac97_vendor_id1 = 0x7c;
    constexpr hcpp::u8 ac97_vendor_id2 = 0x7e;

    constexpr hcpp::u8 prd_count = 8;      // PRDs fit in one page

    struct Cs5535Controller {
        hcpp::u16 iobase;
        bool reset_done;
        bool engine_ready;
        hcpp::u16 codec_vendor;
        hcpp::u32 prd_phys;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *cs5535audio_find() noexcept;
    bool cs5535audio_bind(const pci::PciDevice &device,
                          Cs5535Controller &controller) noexcept;
    bool cs5535audio_reset(Cs5535Controller &controller) noexcept;
    bool cs5535audio_codec_write(Cs5535Controller &controller, hcpp::u8 reg,
                                 hcpp::u16 value) noexcept;
    bool cs5535audio_codec_read(Cs5535Controller &controller, hcpp::u8 reg,
                                hcpp::u16 &value) noexcept;
    bool cs5535audio_present() noexcept;
    bool cs5535audio_probe() noexcept;

}  // namespace hype::audio::cs5535audio
