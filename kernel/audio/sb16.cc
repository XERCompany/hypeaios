#include "kernel/audio/sb16.h"

#include "kernel/arch/x86/io.h"

namespace hype::audio::sb16 {
    namespace {

        constexpr hcpp::u16 dsp_reset_port = 0x6;
        constexpr hcpp::u16 dsp_read_port = 0xA;
        constexpr hcpp::u16 dsp_write_port = 0xC;
        constexpr hcpp::u16 dsp_read_status = 0xE;

        constexpr hcpp::u8 dsp_ready = 0xAA;
        constexpr hcpp::u8 cmd_out8_single = 0x14;
        constexpr hcpp::u8 cmd_set_rate = 0x41;
        constexpr hcpp::u8 cmd_speaker_on = 0xD1;
        constexpr hcpp::u8 cmd_dsp_version = 0xE1;

        constexpr hcpp::u16 dma_status_port = 0x08;
        constexpr hcpp::u16 dma_mask_port = 0x0A;
        constexpr hcpp::u16 dma_mode_port = 0x0B;
        constexpr hcpp::u16 dma_flipflop_port = 0x0C;
        constexpr hcpp::u8 dma_mode_single_out = 0x48;
        constexpr hcpp::u16 dma_addr_ports[4] = {0x00, 0x02, 0x04, 0x06};
        constexpr hcpp::u16 dma_count_ports[4] = {0x01, 0x03, 0x05, 0x07};
        constexpr hcpp::u16 dma_page_ports[4] = {0x87, 0x83, 0x81, 0x82};

        constexpr hcpp::u32 poll_limit = 0x100000;
        constexpr hcpp::u32 dma_poll_limit = 0x1000000;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        bool dsp_write(hcpp::u16 base, hcpp::u8 value) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((hype_port_in8(base + dsp_write_port) & 0x80) == 0) {
                    hype_port_out8(base + dsp_write_port, value);
                    return true;
                }
            }
            return false;
        }

        bool dsp_read(hcpp::u16 base, hcpp::u8 &value) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((hype_port_in8(base + dsp_read_status) & 0x80) != 0) {
                    value = hype_port_in8(base + dsp_read_port);
                    return true;
                }
            }
            return false;
        }

        hcpp::u16 dma_remaining(hcpp::u8 channel) noexcept {
            hype_port_out8(dma_flipflop_port, 0);
            const hcpp::u16 low = hype_port_in8(dma_count_ports[channel]);
            const hcpp::u16 high = hype_port_in8(dma_count_ports[channel]);
            return static_cast<hcpp::u16>(low | (high << 8));
        }

        hcpp::u8 mixer_read(hcpp::u16 base, hcpp::u8 reg) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(base + 4), reg);
            return hype_port_in8(static_cast<hcpp::u16>(base + 5));
        }

        void dma_program(hcpp::u8 channel, hcpp::u32 address, hcpp::u16 count) noexcept {
            const hcpp::u16 length = static_cast<hcpp::u16>(count - 1);
            hype_port_out8(dma_mask_port, static_cast<hcpp::u8>(0x04 | channel));
            hype_port_out8(dma_flipflop_port, 0);
            hype_port_out8(dma_mode_port,
                           static_cast<hcpp::u8>(dma_mode_single_out | channel));
            hype_port_out8(dma_addr_ports[channel], static_cast<hcpp::u8>(address & 0xFF));
            hype_port_out8(dma_addr_ports[channel],
                           static_cast<hcpp::u8>((address >> 8) & 0xFF));
            hype_port_out8(dma_count_ports[channel], static_cast<hcpp::u8>(length & 0xFF));
            hype_port_out8(dma_count_ports[channel],
                           static_cast<hcpp::u8>((length >> 8) & 0xFF));
            hype_port_out8(dma_page_ports[channel],
                           static_cast<hcpp::u8>((address >> 16) & 0xFF));
            hype_port_out8(dma_mask_port, channel);
        }

        bool dma_wait(hcpp::u16 base, hcpp::u8 channel) noexcept {
            const hcpp::u8 irq_bit = channel >= 4 ? 2 : 1;
            for (hcpp::u32 i = 0; i < dma_poll_limit; ++i) {
                if ((mixer_read(base, 0x82) & irq_bit) != 0 ||
                    dma_remaining(channel) == 0xFFFF ||
                    (hype_port_in8(dma_status_port) & (1 << channel)) != 0) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    bool dsp_reset(hcpp::u16 base) noexcept {
        hype_port_out8(base + dsp_reset_port, 1);
        settle(0x400);
        hype_port_out8(base + dsp_reset_port, 0);
        settle(0x400);
        for (hcpp::u32 i = 0; i < poll_limit; ++i) {
            if ((hype_port_in8(base + dsp_read_status) & 0x80) != 0 &&
                hype_port_in8(base + dsp_read_port) == dsp_ready) {
                return true;
            }
        }
        return false;
    }

    bool dsp_version(hcpp::u16 base, hcpp::u8 &major, hcpp::u8 &minor) noexcept {
        if (!dsp_write(base, cmd_dsp_version)) {
            return false;
        }
        return dsp_read(base, major) && dsp_read(base, minor);
    }

    bool dsp_speaker_on(hcpp::u16 base) noexcept {
        return dsp_write(base, cmd_speaker_on);
    }

    bool dsp_set_rate(hcpp::u16 base, hcpp::u16 rate) noexcept {
        return dsp_write(base, cmd_set_rate) &&
               dsp_write(base, static_cast<hcpp::u8>((rate >> 8) & 0xFF)) &&
               dsp_write(base, static_cast<hcpp::u8>(rate & 0xFF));
    }

    bool play_pcm8(hcpp::u16 base, hcpp::u8 dma_channel, hcpp::u16 rate,
                   const hcpp::u8 *samples, hcpp::u16 count) noexcept {
        if (dma_channel > 3 || samples == nullptr || count == 0) {
            return false;
        }
        const hcpp::u32 address =
            static_cast<hcpp::u32>(reinterpret_cast<hcpp::uintptr_t>(samples));
        if (address + count > 0x1000000 || (address & 0xFFFF) + count > 0x10000) {
            return false;
        }
        if (!dsp_set_rate(base, rate)) {
            return false;
        }
        dma_program(dma_channel, address, count);
        const hcpp::u16 length = static_cast<hcpp::u16>(count - 1);
        const bool started = dsp_write(base, cmd_out8_single) &&
                             dsp_write(base, static_cast<hcpp::u8>(length & 0xFF)) &&
                             dsp_write(base, static_cast<hcpp::u8>((length >> 8) & 0xFF));
        if (!started) {
            hype_port_out8(dma_mask_port, static_cast<hcpp::u8>(0x04 | dma_channel));
            return false;
        }
        hype_port_in8(base + dsp_read_status);
        const bool done = dma_wait(base, dma_channel);
        hype_port_in8(base + dsp_read_status);
        hype_port_out8(dma_mask_port, static_cast<hcpp::u8>(0x04 | dma_channel));
        return done;
    }

}  // namespace hype::audio::sb16
