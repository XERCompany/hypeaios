#pragma once

// C-Media CMI8788 "Oxygen" PCI audio (issue #118, wave 2), the chip
// behind the ASUS Xonar/HT Omega/Auzentech AV card families.
// Register map mirrors ALSA sound/pci/oxygen/oxygen_regs.h.
//
// BAR0 is a 256-byte I/O space.  Six independent DMA engines (record
// A/B/C, S/PDIF out, multichannel out, AC'97 out) are driven directly;
// this driver programs the multichannel engine with a real buffer.
// NOTE: the analog DAC behind the CMI8788 is board-specific (SPI/2-wire
// programming per model), so audible output additionally needs model
// codec setup; the DMA engine programming itself is complete.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::oxygen {

    constexpr hcpp::u16 vendor_cmedia = 0x13F6;
    constexpr hcpp::u16 device_cmi8788 = 0x8788;
    constexpr hcpp::u8 io_size = 0x100;

    // DMA channel A/B/C (record), S/PDIF, multichannel, AC'97 bases.
    constexpr hcpp::u8 dma_a_address = 0x00;
    constexpr hcpp::u8 dma_spdif_address = 0x18;
    constexpr hcpp::u8 dma_multich_address = 0x20;
    constexpr hcpp::u8 dma_multich_count = 0x24;
    constexpr hcpp::u8 dma_multich_tcount = 0x28;
    constexpr hcpp::u8 dma_ac97_address = 0x30;

    constexpr hcpp::u8 dma_status = 0x40;
    constexpr hcpp::u8 channel_a = 0x01;
    constexpr hcpp::u8 channel_b = 0x02;
    constexpr hcpp::u8 channel_c = 0x04;
    constexpr hcpp::u8 channel_spdif = 0x08;
    constexpr hcpp::u8 channel_multich = 0x10;
    constexpr hcpp::u8 channel_ac97 = 0x20;
    constexpr hcpp::u8 dma_pause = 0x41;
    constexpr hcpp::u8 dma_reset = 0x42;
    constexpr hcpp::u8 play_channels = 0x43;
    constexpr hcpp::u8 play_channels_2 = 0x00;
    constexpr hcpp::u8 dma_a_burst_8 = 0x00;
    constexpr hcpp::u8 dma_multich_burst_8 = 0x00;

    constexpr hcpp::u8 interrupt_mask = 0x44;
    constexpr hcpp::u8 interrupt_status = 0x46;

    constexpr hcpp::u8 misc = 0x48;
    constexpr hcpp::u8 misc_pci_mem_w_1_clock = 0x20;

    constexpr hcpp::u8 rec_format = 0x4A;
    constexpr hcpp::u8 play_format = 0x4B;
    constexpr hcpp::u8 rec_channels = 0x4C;
    constexpr hcpp::u8 format_16 = 0x00;
    constexpr hcpp::u8 format_24 = 0x01;
    constexpr hcpp::u8 format_32 = 0x02;
    constexpr hcpp::u8 multich_format_shift = 2;

    constexpr hcpp::u8 function = 0x50;
    constexpr hcpp::u8 function_reset_codec = 0x02;
    constexpr hcpp::u8 function_2wire = 0x40;
    constexpr hcpp::u8 function_enable_spi_4_5 = 0x80;

    constexpr hcpp::u8 i2s_multich_format = 0x60;
    constexpr hcpp::u8 i2s_a_format = 0x62;
    constexpr hcpp::u8 i2s_b_format = 0x64;
    constexpr hcpp::u8 i2s_c_format = 0x66;
    constexpr hcpp::u16 i2s_rate_48000 = 0x0002;
    constexpr hcpp::u16 i2s_format_i2s = 0x0000;
    constexpr hcpp::u16 i2s_mclk_256 = 1 << 4;
    constexpr hcpp::u16 i2s_bits_32 = 0x00C0;
    constexpr hcpp::u16 i2s_bclk_64 = 0x0000;

    constexpr hcpp::u8 eeprom_control = 0x80;
    constexpr hcpp::u8 eeprom_dir_read = 0x00;
    constexpr hcpp::u8 eeprom_status = 0x81;
    constexpr hcpp::u8 eeprom_valid = 0x40;
    constexpr hcpp::u8 eeprom_busy = 0x80;
    constexpr hcpp::u8 eeprom_data = 0x82;
    constexpr hcpp::u16 eeprom_id = 0x434D;  // "CM"

    constexpr hcpp::u8 play_routing = 0xC0;
    constexpr hcpp::u8 ac97_control = 0xD0;
    constexpr hcpp::u16 ac97_cold_reset = 0x0001;
    constexpr hcpp::u16 ac97_codec_0 = 0x0010;
    constexpr hcpp::u16 ac97_codec_1 = 0x0020;
    constexpr hcpp::u8 ac97_int_mask = 0xD2;
    constexpr hcpp::u8 ac97_int_status = 0xD3;
    constexpr hcpp::u8 ac97_int_read_done = 0x01;
    constexpr hcpp::u8 ac97_int_write_done = 0x02;
    constexpr hcpp::u8 ac97_regs = 0xDC;
    constexpr hcpp::u32 ac97_reg_addr_shift = 16;
    constexpr hcpp::u32 ac97_reg_dir_read = 0x00800000;
    constexpr hcpp::u32 ac97_reg_codec_shift = 24;

    constexpr hcpp::u8 codec_version = 0xE4;
    constexpr hcpp::u8 revision = 0xE6;
    constexpr hcpp::u16 revision_2 = 0x0008;
    constexpr hcpp::u8 dma_flush = 0xE1;

    // AC'97 mixer registers used at init.
    constexpr hcpp::u8 ac97_reset = 0x00;
    constexpr hcpp::u8 ac97_master = 0x02;
    constexpr hcpp::u8 ac97_powerdown = 0x26;

    constexpr hcpp::u32 pcm_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;
    constexpr hcpp::u8 ring_pages = 4;

    struct OxygenController {
        hcpp::u16 iobase;
        bool has_ac97_0;
        bool has_ac97_1;
        hcpp::u16 chip_revision;
        hcpp::u16 eeprom_id_value;
        bool reset_done;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        bool running;
    };

    const pci::PciDevice *oxygen_find() noexcept;
    bool oxygen_bind(const pci::PciDevice &device,
                     OxygenController &controller) noexcept;
    bool oxygen_reset(OxygenController &controller) noexcept;
    bool oxygen_play(OxygenController &controller) noexcept;
    void oxygen_stop(OxygenController &controller) noexcept;
    hcpp::u32 oxygen_pos(const OxygenController &controller) noexcept;
    bool oxygen_present() noexcept;
    bool oxygen_selftest() noexcept;

}  // namespace hype::audio::oxygen
