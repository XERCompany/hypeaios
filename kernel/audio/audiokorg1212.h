#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// Korg 1212 I/O — rides a PLX PCI 9060-style bridge (10B5:906D).
// Register map mirrors Linux ALSA sound/pci/korg1212/korg1212.c:
// BAR0 is a shared-memory window holding four mailboxes, two
// doorbells, an interrupt status/control register and a mirrored
// device/vendor id register.
//
// Working: identify (+ DEV_VEND_ID readback through the shared
// window), BAR0 claim, mailbox/doorbell handshake, DSP boot-from-
// page-4 command (K1212_DB_BootFromDSPPage4) with a bounded ack
// wait.
// Offline: audio buffer setup (K1212_DB_ConfigureBufferMemory +
// TriggerPlay) — the card reboots its SHARC from on-board flash
// first; host buffer programming is the documented gap.

namespace hype::audio::korg1212 {

    constexpr hcpp::u16 vendor_plx = 0x10b5;
    constexpr hcpp::u16 device_korg1212 = 0x906d;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // Shared-window offsets (BAR0), korg1212.c.
    constexpr hcpp::u32 mailbox0_offset = 0x40;
    constexpr hcpp::u32 mailbox1_offset = 0x44;
    constexpr hcpp::u32 mailbox2_offset = 0x48;
    constexpr hcpp::u32 mailbox3_offset = 0x4c;
    constexpr hcpp::u32 out_doorbell_offset = 0x60;   // host -> card
    constexpr hcpp::u32 in_doorbell_offset = 0x64;    // card -> host
    constexpr hcpp::u32 status_reg_offset = 0x68;     // int ctl/status
    constexpr hcpp::u32 pci_control_offset = 0x6c;
    constexpr hcpp::u32 sens_control_offset = 0x6e;
    constexpr hcpp::u32 dev_vend_id_offset = 0x70;    // id mirror

    constexpr hcpp::u32 pci_int_enable_bit = 0x00000100;
    constexpr hcpp::u32 doorbell_val_mask = 0x00ff;

    // Doorbell command values (K1212_DB_*).
    constexpr hcpp::u8 db_request_data = 0x00;
    constexpr hcpp::u8 db_trigger_play = 0x01;
    constexpr hcpp::u8 db_select_play_mode = 0x02;
    constexpr hcpp::u8 db_configure_buffer_memory = 0x03;
    constexpr hcpp::u8 db_set_clock_source = 0x05;
    constexpr hcpp::u8 db_configure_misc_memory = 0x06;
    constexpr hcpp::u8 db_reboot_card = 0xa0;
    constexpr hcpp::u8 db_boot_from_dsp_page4 = 0xa4;
    constexpr hcpp::u8 db_dsp_download_done = 0xae;
    constexpr hcpp::u8 db_start_dsp_download = 0xaf;

    // cardState values reported through mailbox 3.
    constexpr hcpp::u8 state_uninitialized = 0x01;
    constexpr hcpp::u8 state_ready = 0x04;

    struct Korg1212Controller {
        hcpp::uintptr_t mmio;
        hcpp::u32 id_mirror;
        hcpp::u8 card_state;
        bool reset_done;
        bool engine_ready;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *korg1212_find() noexcept;
    bool korg1212_bind(const pci::PciDevice &device,
                       Korg1212Controller &controller) noexcept;
    bool korg1212_reset(Korg1212Controller &controller) noexcept;
    bool korg1212_present() noexcept;
    bool korg1212_probe() noexcept;

}  // namespace hype::audio::korg1212
