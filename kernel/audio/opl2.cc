#include "kernel/audio/opl2.h"

#include "kernel/arch/x86/io.h"

namespace hype::audio::opl {
    namespace {

        constexpr hcpp::u8 op_offsets[channel_count] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
        constexpr hcpp::u8 patch_mod[5] = {0x01, 0x2C, 0xF3, 0x54, 0x00};
        constexpr hcpp::u8 patch_car[5] = {0x01, 0x00, 0xF6, 0x55, 0x00};
        constexpr hcpp::u8 patch_conn = 0x08;
        constexpr hcpp::u32 opl_clock = 49716;

        void io_delay(hcpp::u16 base, hcpp::u32 reads) noexcept {
            for (hcpp::u32 i = 0; i < reads; ++i) {
                hype_port_in8(base);
            }
        }

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        hcpp::u16 fnum_for(hcpp::u32 freq_hz, hcpp::u8 &block) noexcept {
            for (block = 0; block < 8; ++block) {
                const hcpp::u32 fnum = (freq_hz << (20 - block)) / opl_clock;
                if (fnum <= 1023) {
                    return static_cast<hcpp::u16>(fnum);
                }
            }
            block = 7;
            return 1023;
        }

        void write_op(hcpp::u16 base, hcpp::u8 op, const hcpp::u8 *patch) noexcept {
            opl_write(base, static_cast<hcpp::u8>(0x20 + op), patch[0]);
            opl_write(base, static_cast<hcpp::u8>(0x40 + op), patch[1]);
            opl_write(base, static_cast<hcpp::u8>(0x60 + op), patch[2]);
            opl_write(base, static_cast<hcpp::u8>(0x80 + op), patch[3]);
            opl_write(base, static_cast<hcpp::u8>(0xE0 + op), patch[4]);
        }

    }  // namespace

    void opl_write(hcpp::u16 base, hcpp::u8 reg, hcpp::u8 val) noexcept {
        hype_port_out8(base, reg);
        io_delay(base, 6);
        hype_port_out8(base + 1, val);
        io_delay(base, 35);
    }

    bool opl_detect(hcpp::u16 base) noexcept {
        opl_write(base, 0x04, 0x60);
        opl_write(base, 0x04, 0x80);
        const hcpp::u8 first = hype_port_in8(base) & 0xE0;
        opl_write(base, 0x02, 0xFF);
        opl_write(base, 0x04, 0x21);
        io_delay(base, 64);
        settle(0x40000);
        const hcpp::u8 second = hype_port_in8(base) & 0xE0;
        opl_write(base, 0x04, 0x60);
        opl_write(base, 0x04, 0x80);
        return first == 0x00 && (second & 0xC0) == 0xC0;
    }

    void opl_reset(hcpp::u16 base) noexcept {
        for (hcpp::u16 reg = 0x01; reg <= 0xF5; ++reg) {
            opl_write(base, static_cast<hcpp::u8>(reg), 0);
        }
        opl_write(base, 0x01, 0x20);
        opl_write(base, 0x08, 0x40);
        opl_write(base, 0xBD, 0x00);
    }

    void opl_note(hcpp::u16 base, hcpp::u8 channel, hcpp::u32 freq_hz) noexcept {
        if (channel >= channel_count || freq_hz == 0) {
            return;
        }
        const hcpp::u8 mod = op_offsets[channel];
        const hcpp::u8 car = static_cast<hcpp::u8>(mod + 3);
        write_op(base, mod, patch_mod);
        write_op(base, car, patch_car);
        opl_write(base, static_cast<hcpp::u8>(0xC0 + channel), patch_conn);
        hcpp::u8 block = 0;
        const hcpp::u16 fnum = fnum_for(freq_hz, block);
        opl_write(base, static_cast<hcpp::u8>(0xA0 + channel),
                  static_cast<hcpp::u8>(fnum & 0xFF));
        opl_write(base, static_cast<hcpp::u8>(0xB0 + channel),
                  static_cast<hcpp::u8>(0x20 | ((block & 7) << 2) | ((fnum >> 8) & 0x3)));
    }

    void opl_note_off(hcpp::u16 base, hcpp::u8 channel) noexcept {
        if (channel >= channel_count) {
            return;
        }
        opl_write(base, static_cast<hcpp::u8>(0xB0 + channel), 0);
    }

}  // namespace hype::audio::opl
