#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

// ICEnsemble ICE1712 (Envy24) — four I/O BARs:
//   BAR0 control/status register file (ICEREG)
//   BAR1 consumer-path DS DMA window
//   BAR2 DS DMA path registers
//   BAR3 professional multi-track registers (ICEMT)
// Register map mirrors Linux ALSA sound/pci/ice1712/ice1712.h and
// the init order of snd_ice1712_chip_init().
//
// Working: identify, BAR0-BAR3 claim, soft reset into native mode,
// AC'97 warm reset + codec write/read, indirect register writes.
// Offline: consumer/professional DMA engines — the DS channel map
// and multi-track FIFOs are left programmed but not started.

namespace hype::audio::ice1712 {

    constexpr hcpp::u16 vendor_icensemble = 0x1412;
    constexpr hcpp::u16 device_ice1712 = 0x1712;

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    // BAR0 direct registers.
    constexpr hcpp::u8 reg_control = 0x00;       // u8
    constexpr hcpp::u8 ctrl_reset = 0x80;        // soft reset whole chip
    constexpr hcpp::u8 ctrl_native = 0x01;       // native (not SB-emul) mode
    constexpr hcpp::u8 reg_irqmask = 0x01;       // u8
    constexpr hcpp::u8 reg_irqstat = 0x02;       // u8
    constexpr hcpp::u8 reg_index = 0x03;         // u8 indirect index
    constexpr hcpp::u8 reg_data = 0x04;          // u8 indirect data
    constexpr hcpp::u8 reg_ac97_index = 0x08;    // u8 codec reg index
    constexpr hcpp::u8 reg_ac97_cmd = 0x09;      // u8 codec command
    constexpr hcpp::u8 reg_ac97_data = 0x0a;     // u16 codec data

    constexpr hcpp::u8 ac97_cold = 0x80;         // cold reset pulse
    constexpr hcpp::u8 ac97_warm = 0x40;         // warm reset pulse
    constexpr hcpp::u8 ac97_write = 0x20;        // write strobe/busy
    constexpr hcpp::u8 ac97_read = 0x10;         // read strobe/busy
    constexpr hcpp::u8 ac97_ready = 0x08;        // codec ready status

    // Indirect (IREG) registers, selected via INDEX/DATA.
    constexpr hcpp::u8 ireg_gpio_data = 0x20;
    constexpr hcpp::u8 ireg_gpio_write_mask = 0x21;
    constexpr hcpp::u8 ireg_gpio_direction = 0x22;
    constexpr hcpp::u8 ireg_consumer_powerdown = 0x30;
    constexpr hcpp::u8 ireg_pro_powerdown = 0x31;

    // AC'97 codec registers.
    constexpr hcpp::u8 ac97_reg_reset = 0x00;
    constexpr hcpp::u8 ac97_reg_master_volume = 0x02;
    constexpr hcpp::u8 ac97_reg_pcmout_volume = 0x18;
    constexpr hcpp::u8 ac97_reg_vendor_id1 = 0x7c;
    constexpr hcpp::u8 ac97_reg_vendor_id2 = 0x7e;

    constexpr hcpp::u8 ring_pages = 4;

    struct Ice1712Controller {
        hcpp::u16 iobase;      // BAR0
        hcpp::u16 ddma_port;   // BAR1
        hcpp::u16 dmapath_port;// BAR2
        hcpp::u16 profi_port;  // BAR3
        bool reset_done;
        bool engine_ready;
        hcpp::u16 codec_vendor;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
    };

    const pci::PciDevice *ice1712_find() noexcept;
    bool ice1712_bind(const pci::PciDevice &device,
                      Ice1712Controller &controller) noexcept;
    bool ice1712_reset(Ice1712Controller &controller) noexcept;
    bool ice1712_codec_write(Ice1712Controller &controller, hcpp::u8 reg,
                             hcpp::u16 value) noexcept;
    bool ice1712_codec_read(Ice1712Controller &controller, hcpp::u8 reg,
                            hcpp::u16 &value) noexcept;
    bool ice1712_present() noexcept;
    bool ice1712_probe() noexcept;

}  // namespace hype::audio::ice1712
