#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Creative CA0106 (SB Audigy LS / Live! 24-bit / SB0570).
// Register map mirrors Linux ALSA sound/pci/ca0106/ca0106.h:
//  - I/O BAR0 flat registers (PTR/DATA/IPR/INTE/HCFG/GPIO/AC97)
//  - indexed channel registers through PTR=(reg<<16)|channel, DATA
// Init values below are taken from snd_ca0106_chip_init().
//
// Working: identify, BAR0 claim, chip init (routing/volumes/GPIO/
// AC'97-enable + readback), playback engine programming into a PMM
// period-list (PLAYBACK_LIST_* registers).
// Offline: the trigger word that actually starts playback — the
// ALSA start path funnels through the DSP "channel enable" bits in
// extended-interrupt space whose exact write is not verified here,
// so the engine is left armed-but-stopped (engine_ready reports
// the list is programmed, running stays false).

namespace hype::audio::ca0106 {

    constexpr hcpp::u16 vendor_creative = 0x1102;
    constexpr hcpp::u16 device_ca0106 = 0x0007;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // Flat I/O registers (BAR0).
    constexpr hcpp::u8 reg_ptr = 0x00;          // u32 indexed pointer
    constexpr hcpp::u8 reg_data = 0x04;         // u32 indexed data
    constexpr hcpp::u8 reg_ipr = 0x08;          // u32 interrupt pending
    constexpr hcpp::u8 reg_inte = 0x0c;         // u32 interrupt enable
    constexpr hcpp::u8 reg_gpio = 0x18;         // u32 GPIO
    constexpr hcpp::u8 reg_hcfg = 0x14;         // u32 hardware config
    constexpr hcpp::u8 reg_ac97data = 0x1c;     // u16 AC'97 data
    constexpr hcpp::u8 reg_ac97address = 0x1e;  // u8 AC'97 register index

    // HCFG bits (ca0106.h).
    constexpr hcpp::u32 hcfg_ac97 = 0x00000008;      // AC'97 2.0 mode
    constexpr hcpp::u32 hcfg_lock_playback = 0x00000004;
    constexpr hcpp::u32 hcfg_lock_capture = 0x00000002;
    constexpr hcpp::u32 hcfg_audioenable = 0x00000001;

    // Indexed (per-channel) registers.
    constexpr hcpp::u8 ireg_playback_list_addr = 0x00;
    constexpr hcpp::u8 ireg_playback_list_size = 0x01;
    constexpr hcpp::u8 ireg_playback_list_ptr = 0x02;
    constexpr hcpp::u8 ireg_playback_dma_addr = 0x04;
    constexpr hcpp::u8 ireg_playback_period_size = 0x05;
    constexpr hcpp::u8 ireg_playback_pointer = 0x06;
    constexpr hcpp::u8 ireg_playback_fifo_offset = 0x08;
    constexpr hcpp::u8 ireg_capture_dma_addr = 0x10;
    constexpr hcpp::u8 ireg_capture_buffer_size = 0x11;
    constexpr hcpp::u8 ireg_capture_pointer = 0x12;
    constexpr hcpp::u8 ireg_basic_interrupt = 0x40;
    constexpr hcpp::u8 ireg_spdif_select1 = 0x45;
    constexpr hcpp::u8 ireg_spdif_select2 = 0x46;
    constexpr hcpp::u8 ireg_capture_source = 0x60;
    constexpr hcpp::u8 ireg_capture_volume1 = 0x61;
    constexpr hcpp::u8 ireg_capture_volume2 = 0x62;
    constexpr hcpp::u8 ireg_playback_routing1 = 0x63;
    constexpr hcpp::u8 ireg_playback_routing2 = 0x64;
    constexpr hcpp::u8 ireg_playback_mute = 0x65;
    constexpr hcpp::u8 ireg_playback_volume1 = 0x66;
    constexpr hcpp::u8 ireg_capture_routing1 = 0x67;
    constexpr hcpp::u8 ireg_capture_routing2 = 0x68;
    constexpr hcpp::u8 ireg_playback_volume2 = 0x6a;
    constexpr hcpp::u8 ireg_capture_control = 0x71;
    constexpr hcpp::u8 ireg_extended_int_mask = 0x75;

    // GPIO init values from snd_ca0106_chip_init() (analog out).
    constexpr hcpp::u32 gpio_analog_default = 0x005f03a3;
    constexpr hcpp::u32 gpio_analog_sb041x = 0x005f5301;

    constexpr hcpp::u8 play_channels = 4;       // channels 0-3 playback
    constexpr hcpp::u8 list_periods = 8;        // period-pointer list depth

    struct Ca0106Controller {
        hcpp::u16 iobase;
        bool reset_done;
        bool engine_ready;   // period list + channel regs programmed
        bool running;        // trigger issued (unverified path)
        hcpp::u32 list_phys;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *ca0106_find() noexcept;
    bool ca0106_bind(const pci::PciDevice &device,
                     Ca0106Controller &controller) noexcept;
    bool ca0106_reset(Ca0106Controller &controller) noexcept;
    bool ca0106_present() noexcept;
    bool ca0106_probe() noexcept;

}  // namespace hype::audio::ca0106
