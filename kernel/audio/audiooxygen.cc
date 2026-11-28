#include "kernel/audio/audiooxygen.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiow2.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::oxygen {
    namespace {

        OxygenController active{};
        bool present;

        hcpp::u8 read8(const OxygenController &c, hcpp::u8 reg) noexcept {
            return hype_port_in8(static_cast<hcpp::u16>(c.iobase + reg));
        }

        hcpp::u16 read16(const OxygenController &c, hcpp::u8 reg) noexcept {
            return hype_port_in16(static_cast<hcpp::u16>(c.iobase + reg));
        }

        hcpp::u32 read32(const OxygenController &c, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(c.iobase + reg));
        }

        void write8(const OxygenController &c, hcpp::u8 reg,
                    hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void write16(const OxygenController &c, hcpp::u8 reg,
                     hcpp::u16 value) noexcept {
            hype_port_out16(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void write32(const OxygenController &c, hcpp::u8 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void write8_masked(const OxygenController &c, hcpp::u8 reg,
                           hcpp::u8 value, hcpp::u8 mask) noexcept {
            write8(c, reg, static_cast<hcpp::u8>((read8(c, reg) & ~mask) |
                                                 (value & mask)));
        }

        hcpp::u16 eeprom_read(const OxygenController &c,
                              hcpp::u8 index) noexcept {
            write8(c, eeprom_control,
                   static_cast<hcpp::u8>((index & 0x7F) | eeprom_dir_read));
            for (hcpp::u32 i = 0; i < w2::poll_limit >> 6; ++i) {
                const hcpp::u8 status = read8(c, eeprom_status);
                if ((status & eeprom_busy) == 0) {
                    return (status & eeprom_valid) != 0
                               ? read16(c, eeprom_data)
                               : static_cast<hcpp::u16>(0xFFFF);
                }
                asm volatile("pause");
            }
            return 0xFFFF;
        }

        bool ac97_wait(const OxygenController &c, hcpp::u8 mask) noexcept {
            hcpp::u8 status = 0;
            for (hcpp::u32 i = 0; i < w2::poll_limit >> 6; ++i) {
                // Reading the status register also clears its bits.
                status |= read8(c, ac97_int_status);
                if ((status & mask) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool ac97_read_reg(const OxygenController &c, hcpp::u8 codec,
                           hcpp::u8 index, hcpp::u16 &value) noexcept {
            const hcpp::u32 reg =
                (static_cast<hcpp::u32>(index) << ac97_reg_addr_shift) |
                ac97_reg_dir_read |
                (static_cast<hcpp::u32>(codec) << ac97_reg_codec_shift);
            for (hcpp::u32 tries = 0; tries < 5; ++tries) {
                write32(c, ac97_regs, reg);
                if (ac97_wait(c, ac97_int_read_done)) {
                    value = read16(c, ac97_regs);
                    return true;
                }
            }
            return false;
        }

        bool ac97_write_reg(const OxygenController &c, hcpp::u8 codec,
                            hcpp::u8 index, hcpp::u16 data) noexcept {
            const hcpp::u32 reg =
                static_cast<hcpp::u32>(data) |
                (static_cast<hcpp::u32>(index) << ac97_reg_addr_shift) |
                (static_cast<hcpp::u32>(codec) << ac97_reg_codec_shift);
            for (hcpp::u32 tries = 0; tries < 5; ++tries) {
                write32(c, ac97_regs, reg);
                if (ac97_wait(c, ac97_int_write_done)) {
                    return true;
                }
            }
            return false;
        }

        bool ensure_buffer(OxygenController &c) noexcept {
            if (c.buffer_phys != 0) {
                return true;
            }
            c.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(ring_pages));
            if (c.buffer_phys == 0) {
                return false;
            }
            c.buffer_bytes = memory::page_size * ring_pages;
            return true;
        }

        // Render a stereo sine as 32-bit (24-in-32) samples.
        void render_sine32(hcpp::u32 phys, hcpp::u32 frames) noexcept {
            auto *dst = reinterpret_cast<hcpp::i32 *>(
                static_cast<hcpp::uintptr_t>(phys));
            hcpp::i16 tmp[2];
            const hcpp::u32 phase_step =
                (tone_freq_hz * 65536u + pcm_rate / 2) / pcm_rate;
            hcpp::u32 phase = 0;
            for (hcpp::u32 i = 0; i < frames; ++i) {
                const hcpp::i16 s =
                    waveform_sine_q15(static_cast<hcpp::u16>(phase >> 16));
                tmp[0] = s;
                tmp[1] = s;
                for (hcpp::u8 ch = 0; ch < 2; ++ch) {
                    dst[i * 2 + ch] = static_cast<hcpp::i32>(tmp[ch]) << 16;
                }
                phase += phase_step;
            }
        }

    }  // namespace

    const pci::PciDevice *oxygen_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_cmi8788};
        return w2::find_ids(vendor_cmedia, ids, 1);
    }

    bool oxygen_bind(const pci::PciDevice &device,
                     OxygenController &controller) noexcept {
        controller = {};
        if (!w2::enable(device, true, false)) {
            return false;
        }
        controller.iobase = w2::io_base(device, 0);
        return controller.iobase != 0;
    }

    bool oxygen_reset(OxygenController &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        controller.chip_revision = read16(controller, revision);
        if ((controller.chip_revision & revision_2) == 0) {
            write8_masked(controller, misc, misc_pci_mem_w_1_clock,
                          misc_pci_mem_w_1_clock);
        }
        controller.eeprom_id_value = eeprom_read(controller, 0);

        // AC'97 codec presence flags.
        const hcpp::u16 ac97 = read16(controller, ac97_control);
        controller.has_ac97_0 = (ac97 & ac97_codec_0) != 0;
        controller.has_ac97_1 = (ac97 & ac97_codec_1) != 0;

        // oxygen_init: assert codec reset, stop engines, set defaults.
        write8_masked(controller, function,
                      function_reset_codec,
                      function_reset_codec | function_2wire |
                          function_enable_spi_4_5);
        write8(controller, dma_status, 0);
        write8(controller, dma_pause, 0);
        write8(controller, play_channels,
               play_channels_2 | dma_a_burst_8 | dma_multich_burst_8);
        write8(controller, play_format,
               format_32 << multich_format_shift);
        write8(controller, rec_format, 0);
        write8(controller, rec_channels, 0);
        write16(controller, interrupt_mask, 0);
        write16(controller, i2s_multich_format,
                i2s_rate_48000 | i2s_format_i2s | i2s_mclk_256 |
                    i2s_bits_32 | i2s_bclk_64);
        w2::settle(w2::settle_spins >> 2);
        if (controller.has_ac97_0) {
            ac97_write_reg(controller, 0, ac97_reset, 0);
            ac97_write_reg(controller, 0, ac97_master, 0x0000);
            ac97_write_reg(controller, 0, ac97_powerdown, 0);
        }
        controller.reset_done = true;
        return true;
    }

    bool oxygen_play(OxygenController &controller) noexcept {
        if (!controller.reset_done || !ensure_buffer(controller)) {
            return false;
        }
        const hcpp::u32 frames =
            controller.buffer_bytes / (pcm_channels * sizeof(hcpp::i32));
        render_sine32(controller.buffer_phys, frames);
        write32(controller, dma_multich_address, controller.buffer_phys);
        write32(controller, dma_multich_count,
                controller.buffer_bytes / 4 - 1);
        write32(controller, dma_multich_tcount,
                controller.buffer_bytes / 8 - 1);
        // Flush the multichannel FIFO then start it.
        write8(controller, dma_flush, channel_multich);
        write8(controller, dma_flush, 0);
        write8(controller, dma_status,
               static_cast<hcpp::u8>(read8(controller, dma_status) |
                                   channel_multich));
        controller.running =
            (read8(controller, dma_status) & channel_multich) != 0;
        return controller.running;
    }

    void oxygen_stop(OxygenController &controller) noexcept {
        if (controller.iobase != 0) {
            write8(controller, dma_status,
                   static_cast<hcpp::u8>(read8(controller, dma_status) &
                                         ~channel_multich));
            write8(controller, dma_flush, channel_multich);
            write8(controller, dma_flush, 0);
        }
        controller.running = false;
    }

    hcpp::u32 oxygen_pos(const OxygenController &controller) noexcept {
        if (!controller.running || controller.iobase == 0) {
            return 0;
        }
        // All DMA base registers return the current position on read.
        const hcpp::u32 curr = read32(controller, dma_multich_address);
        return curr >= controller.buffer_phys
                   ? curr - controller.buffer_phys
                   : 0;
    }

    bool oxygen_present() noexcept { return present; }

    bool oxygen_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = oxygen_find();
        if (device == nullptr) {
            serial::write_line("audiopci oxygen=absent");
            return true;
        }
        if (!oxygen_bind(*device, active) || !oxygen_reset(active)) {
            serial::write_line("audiopci oxygen=fail");
            return false;
        }
        serial::write("audiopci oxygen rev=0x");
        w2::hex16(active.chip_revision);
        serial::write(" ee=0x");
        w2::hex16(active.eeprom_id_value);
        serial::write(" ac97=");
        serial::write_dec32(static_cast<hcpp::u32>(active.has_ac97_0) +
                            static_cast<hcpp::u32>(active.has_ac97_1));
        serial::write("\r\n");
        if (active.has_ac97_0) {
            hcpp::u16 caps = 0;
            if (ac97_read_reg(active, 0, ac97_reset, caps)) {
                serial::write("audiopci oxygen codec=0x");
                w2::hex16(caps);
                serial::write("\r\n");
            }
        }
        if (!oxygen_play(active)) {
            serial::write_line("audiopci oxygen dma=fail");
            return false;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 256 && pos == 0; ++round) {
            w2::settle(0x80000);
            pos = oxygen_pos(active);
        }
        serial::write("audiopci oxygen pos=");
        w2::hex32(pos);
        serial::write("\r\n");
        oxygen_stop(active);
        present = true;
        serial::write_line("audiopci oxygen=ok");
        return pos != 0;
    }

}  // namespace hype::audio::oxygen
