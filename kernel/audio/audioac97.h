#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::ac97 {

    constexpr hcpp::u16 vendor_intel = 0x8086;
    constexpr hcpp::u16 device_82801aa = 0x2415;
    constexpr hcpp::u8 class_multimedia = 0x04;
    constexpr hcpp::u8 subclass_ac97 = 0x01;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    constexpr hcpp::u16 nam_reset = 0x00;
    constexpr hcpp::u16 nam_master_volume = 0x02;
    constexpr hcpp::u16 nam_pcm_volume = 0x18;
    constexpr hcpp::u16 nam_powerdown = 0x26;
    constexpr hcpp::u16 nam_ext_audio = 0x28;
    constexpr hcpp::u16 nam_front_dac_rate = 0x2C;
    constexpr hcpp::u16 nam_vendor_id1 = 0x7C;
    constexpr hcpp::u16 nam_vendor_id2 = 0x7E;

    constexpr hcpp::u8 nabm_po_bdbar = 0x10;
    constexpr hcpp::u8 nabm_po_civ = 0x14;
    constexpr hcpp::u8 nabm_po_lvi = 0x15;
    constexpr hcpp::u8 nabm_po_sr = 0x16;
    constexpr hcpp::u8 nabm_po_picb = 0x18;
    constexpr hcpp::u8 nabm_po_piv = 0x1A;
    constexpr hcpp::u8 nabm_po_cr = 0x1B;
    constexpr hcpp::u8 nabm_glob_cnt = 0x2C;
    constexpr hcpp::u8 nabm_glob_sta = 0x30;
    constexpr hcpp::u8 nabm_cas = 0x34;

    constexpr hcpp::u8 cr_rpbm = 0x01;
    constexpr hcpp::u8 cr_rr = 0x02;
    constexpr hcpp::u8 cr_ioce = 0x10;
    constexpr hcpp::u16 sr_dch = 0x0001;
    constexpr hcpp::u32 cnt_cold_reset = 0x2;
    constexpr hcpp::u32 cnt_warm_reset = 0x4;
    constexpr hcpp::u32 sta_primary_ready = 0x100;

    constexpr hcpp::u16 vol_mute = 0x8000;
    constexpr hcpp::u16 vol_mask = 0x3F3F;
    constexpr hcpp::u16 bd_flag_bup = 0x4000;
    constexpr hcpp::u16 bd_flag_ioc = 0x8000;
    constexpr hcpp::u8 bd_entries = 32;

    constexpr hcpp::u32 pcm_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;

    struct Ac97Descriptor {
        hcpp::u32 address;
        hcpp::u16 samples;
        hcpp::u16 flags;
    };

    struct Ac97Controller {
        hcpp::u16 nambar;
        hcpp::u16 nabmbar;
        hcpp::u8 card_index;
        hcpp::u32 codec_id;
        bool reset_done;
        hcpp::u32 bd_phys;
        volatile Ac97Descriptor *bd;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 buffer_frames;
        bool running;
    };

    const pci::PciDevice *ac97_find() noexcept;
    bool ac97_bind(const pci::PciDevice &device, Ac97Controller &controller) noexcept;
    bool ac97_bind_card(hcpp::size_t card_index, Ac97Controller &controller) noexcept;
    bool ac97_reset(Ac97Controller &controller) noexcept;
    bool ac97_play(Ac97Controller &controller, hcpp::u32 frames) noexcept;
    void ac97_stop(Ac97Controller &controller) noexcept;
    hcpp::u32 ac97_pos(const Ac97Controller &controller) noexcept;
    bool ac97_set_master_volume(Ac97Controller &controller, hcpp::u8 percent) noexcept;
    bool ac97_set_pcm_volume(Ac97Controller &controller, hcpp::u8 percent) noexcept;
    bool ac97_present() noexcept;
    bool ac97_ready() noexcept;
    bool ac97_selftest() noexcept;

}  // namespace hype::audio::ac97
