#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::es1370 {

    constexpr hcpp::u16 vendor_ensoniq = 0x1274;
    constexpr hcpp::u16 device_es1370 = 0x5000;
    constexpr hcpp::u8 class_multimedia = 0x04;
    constexpr hcpp::u8 subclass_audio = 0x01;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    constexpr hcpp::u8 reg_control = 0x00;
    constexpr hcpp::u8 reg_status = 0x04;
    constexpr hcpp::u8 reg_uart_data = 0x08;
    constexpr hcpp::u8 reg_uart_status = 0x09;
    constexpr hcpp::u8 reg_uart_test = 0x0A;
    constexpr hcpp::u8 reg_mempage = 0x0C;
    constexpr hcpp::u8 reg_codec = 0x10;
    constexpr hcpp::u8 reg_serial = 0x20;
    constexpr hcpp::u8 reg_dac1_scount = 0x24;
    constexpr hcpp::u8 reg_dac2_scount = 0x28;
    constexpr hcpp::u8 reg_adc_scount = 0x2C;

    constexpr hcpp::u8 page_dac = 0x0C;
    constexpr hcpp::u8 page_adc = 0x0D;
    constexpr hcpp::u8 page_dac1_frame_addr = 0x30;
    constexpr hcpp::u8 page_dac1_frame_cnt = 0x34;
    constexpr hcpp::u8 page_dac2_frame_addr = 0x38;
    constexpr hcpp::u8 page_dac2_frame_cnt = 0x3C;
    constexpr hcpp::u8 page_adc_frame_addr = 0x30;
    constexpr hcpp::u8 page_adc_frame_cnt = 0x34;

    constexpr hcpp::u32 ctrl_adc_stop = 0x80000000;
    constexpr hcpp::u32 ctrl_xctl1 = 0x40000000;
    constexpr hcpp::u32 ctrl_pclkdiv = 0x1FFF0000;
    constexpr hcpp::u32 ctrl_msfmt_sel = 0x00008000;
    constexpr hcpp::u32 ctrl_m_sbb = 0x00004000;
    constexpr hcpp::u32 ctrl_wtsrsel = 0x00003000;
    constexpr hcpp::u8 ctrl_sh_wtsrsel = 12;
    constexpr hcpp::u32 ctrl_dac_sync = 0x00000800;
    constexpr hcpp::u32 ctrl_ccb_intrm = 0x00000400;
    constexpr hcpp::u32 ctrl_m_cb = 0x00000200;
    constexpr hcpp::u32 ctrl_xctl0 = 0x00000100;
    constexpr hcpp::u32 ctrl_dac1_en = 0x00000040;
    constexpr hcpp::u32 ctrl_dac2_en = 0x00000020;
    constexpr hcpp::u32 ctrl_adc_en = 0x00000010;
    constexpr hcpp::u32 ctrl_uart_en = 0x00000008;
    constexpr hcpp::u32 ctrl_jystk_en = 0x00000004;
    constexpr hcpp::u32 ctrl_cdc_en = 0x00000002;
    constexpr hcpp::u32 ctrl_serr_dis = 0x00000001;

    constexpr hcpp::u32 stat_intr = 0x80000000;
    constexpr hcpp::u32 stat_cstat = 0x00000400;
    constexpr hcpp::u32 stat_cbusy = 0x00000200;
    constexpr hcpp::u32 stat_cwrip = 0x00000100;
    constexpr hcpp::u32 stat_uart = 0x00000008;
    constexpr hcpp::u32 stat_dac1 = 0x00000004;
    constexpr hcpp::u32 stat_dac2 = 0x00000002;
    constexpr hcpp::u32 stat_adc = 0x00000001;

    constexpr hcpp::u32 sctrl_p2endinc = 0x00380000;
    constexpr hcpp::u32 sctrl_p2stinc = 0x00070000;
    constexpr hcpp::u32 sctrl_r1loopsel = 0x00008000;
    constexpr hcpp::u32 sctrl_p2loopsel = 0x00004000;
    constexpr hcpp::u32 sctrl_p1loopsel = 0x00002000;
    constexpr hcpp::u32 sctrl_p2pause = 0x00001000;
    constexpr hcpp::u32 sctrl_p1pause = 0x00000800;
    constexpr hcpp::u32 sctrl_r1inten = 0x00000400;
    constexpr hcpp::u32 sctrl_p2inten = 0x00000200;
    constexpr hcpp::u32 sctrl_p1inten = 0x00000100;
    constexpr hcpp::u32 sctrl_p1sctrld = 0x00000080;
    constexpr hcpp::u32 sctrl_p2dacsen = 0x00000040;
    constexpr hcpp::u32 sctrl_r1fmt = 0x00000030;
    constexpr hcpp::u32 sctrl_p2fmt = 0x0000000C;
    constexpr hcpp::u32 sctrl_p1fmt = 0x00000003;
    constexpr hcpp::u32 sctrl_fmt_u8m = 0x0;
    constexpr hcpp::u32 sctrl_fmt_u8s = 0x1;
    constexpr hcpp::u32 sctrl_fmt_s16m = 0x2;
    constexpr hcpp::u32 sctrl_fmt_s16s = 0x3;

    constexpr hcpp::u8 ak_master_lch = 0x00;
    constexpr hcpp::u8 ak_master_rch = 0x01;
    constexpr hcpp::u8 ak_pcm_lch = 0x12;
    constexpr hcpp::u8 ak_pcm_rch = 0x13;
    constexpr hcpp::u8 ak_mute = 0x80;
    constexpr hcpp::u8 codec_regs = 0x20;

    constexpr hcpp::u32 dac1_rate_5512 = 0;
    constexpr hcpp::u32 dac1_rate_11025 = 1;
    constexpr hcpp::u32 dac1_rate_22050 = 2;
    constexpr hcpp::u32 dac1_rate_44100 = 3;
    constexpr hcpp::u32 pcm_rate = 44100;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;
    constexpr hcpp::u8 ring_pages = 4;

    struct Es1370Controller {
        hcpp::u16 iobase;
        hcpp::u8 card_index;
        bool reset_done;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 buffer_frames;
        hcpp::u32 buffer_dwords;
        bool running;
        hcpp::u8 volume;
        hcpp::u8 codec_shadow[codec_regs];
    };

    const pci::PciDevice *es1370_find() noexcept;
    bool es1370_bind(const pci::PciDevice &device, Es1370Controller &controller) noexcept;
    bool es1370_reset(Es1370Controller &controller) noexcept;
    bool es1370_codec_write(Es1370Controller &controller, hcpp::u8 reg,
                            hcpp::u8 value) noexcept;
    hcpp::u8 es1370_codec_read(const Es1370Controller &controller, hcpp::u8 reg) noexcept;
    bool es1370_play(Es1370Controller &controller, hcpp::u32 frames) noexcept;
    void es1370_stop(Es1370Controller &controller) noexcept;
    hcpp::u32 es1370_pos(Es1370Controller &controller) noexcept;
    bool es1370_volume(Es1370Controller &controller, hcpp::u8 percent) noexcept;
    bool es1370_present() noexcept;
    bool es1370_ready() noexcept;
    bool es1370_selftest() noexcept;

}  // namespace hype::audio::es1370
