#include "kernel/input/msaux.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/io.h"

namespace hype::input {

    namespace {

        constexpr hcpp::u16 port_data = 0x60;
        constexpr hcpp::u16 port_status = 0x64;
        constexpr hcpp::u16 port_command = 0x64;
        constexpr hcpp::u8 status_output_full = 0x01;
        constexpr hcpp::u8 status_input_full = 0x02;
        constexpr hcpp::u8 status_aux_data = 0x20;
        constexpr hcpp::u8 ctl_enable_aux = 0xA8;
        constexpr hcpp::u8 ctl_read_cmdbyte = 0x20;
        constexpr hcpp::u8 ctl_write_cmdbyte = 0x60;
        constexpr hcpp::u8 ctl_write_aux = 0xD4;
        constexpr hcpp::u8 dev_set_defaults = 0xF6;
        constexpr hcpp::u8 dev_set_sample_rate = 0xF3;
        constexpr hcpp::u8 dev_enable_stream = 0xF4;
        constexpr hcpp::u8 dev_ack = 0xFA;
        constexpr hcpp::u32 io_spin = 100000;

        bool ready;
        hcpp::u64 bytes_seen;
        hcpp::u64 resyncs;
        hcpp::u8 assembly[3];
        hcpp::u8 assembly_len;
        hcpp::u8 fail_step;

        bool wait_input_clear() noexcept {
            for (hcpp::u32 i = 0; i < io_spin; ++i) {
                if ((hype_port_in8(port_status) & status_input_full) == 0) {
                    return true;
                }
                hype_io_wait();
            }
            return false;
        }

        bool wait_output_full() noexcept {
            for (hcpp::u32 i = 0; i < io_spin; ++i) {
                if ((hype_port_in8(port_status) & status_output_full) != 0) {
                    return true;
                }
                hype_io_wait();
            }
            return false;
        }

        void drain_output() noexcept {
            for (hcpp::u32 i = 0; i < 64; ++i) {
                if ((hype_port_in8(port_status) & status_output_full) == 0) {
                    return;
                }
                hype_port_in8(port_data);
            }
        }

        bool controller_write(hcpp::u8 value) noexcept {
            if (!wait_input_clear()) {
                return false;
            }
            hype_port_out8(port_command, value);
            return true;
        }

        bool data_write(hcpp::u8 value) noexcept {
            if (!wait_input_clear()) {
                return false;
            }
            hype_port_out8(port_data, value);
            return true;
        }

        bool aux_send(hcpp::u8 value, bool *acked = nullptr) noexcept {
            if (!controller_write(ctl_write_aux)) {
                return false;
            }
            if (!data_write(value)) {
                return false;
            }
            if (acked == nullptr) {
                return true;
            }
            if (!wait_output_full()) {
                *acked = false;
                return true;
            }
            *acked = hype_port_in8(port_data) == dev_ack;
            return true;
        }

    }

    bool msaux_parse(hcpp::u8 b0, hcpp::u8 b1, hcpp::u8 b2, MsauxPacket &out) noexcept {
        if ((b0 & 0x08) == 0) {
            return false;
        }
        hcpp::i32 dx = b1;
        if ((b0 & 0x10) != 0) {
            dx -= 256;
        }
        hcpp::i32 dy = b2;
        if ((b0 & 0x20) != 0) {
            dy -= 256;
        }
        dy = -dy;
        if ((b0 & 0x40) != 0) {
            dx = 0;
        }
        if ((b0 & 0x80) != 0) {
            dy = 0;
        }
        out.buttons = static_cast<hcpp::u8>(b0 & 0x07);
        out.dx = dx;
        out.dy = dy;
        return true;
    }

    namespace {

        bool msaux_init_body() noexcept {
            ready = false;
            bytes_seen = 0;
            resyncs = 0;
            assembly_len = 0;
            fail_step = 0;
            drain_output();
            if (!controller_write(ctl_enable_aux)) {
                fail_step = 1;
                return false;
            }
            if (!controller_write(ctl_read_cmdbyte) || !wait_output_full()) {
                fail_step = 2;
                return false;
            }
            hcpp::u8 cmdbyte = hype_port_in8(port_data);
            cmdbyte = static_cast<hcpp::u8>(cmdbyte & ~0x22);
            if (!controller_write(ctl_write_cmdbyte) || !data_write(cmdbyte)) {
                fail_step = 3;
                return false;
            }
            drain_output();
            bool acked = false;
            if (!aux_send(dev_set_defaults, &acked) || !acked) {
                fail_step = 4;
                return false;
            }
            if (!aux_send(dev_set_sample_rate, &acked) || !acked) {
                fail_step = 5;
                return false;
            }
            if (!aux_send(100, &acked) || !acked) {
                fail_step = 6;
                return false;
            }
            if (!aux_send(dev_enable_stream, &acked) || !acked) {
                fail_step = 7;
                return false;
            }
            ready = true;
            return true;
        }

    }

    bool msaux_init() noexcept {
        hype_cpu_disable_interrupts();
        const bool ok = msaux_init_body();
        hype_cpu_enable_interrupts();
        return ok;
    }

    bool msaux_ready() noexcept { return ready; }

    hcpp::u32 msaux_poll(MsauxPacket *out, hcpp::u32 max) noexcept {
        hcpp::u32 done = 0;
        if (!ready) {
            return 0;
        }
        for (hcpp::u32 guard = 0; guard < 64; ++guard) {
            const hcpp::u8 st = hype_port_in8(port_status);
            if ((st & status_output_full) == 0) {
                break;
            }
            if ((st & status_aux_data) == 0) {
                break;
            }
            const hcpp::u8 b = hype_port_in8(port_data);
            ++bytes_seen;
            if (assembly_len == 0 && (b & 0x08) == 0) {
                ++resyncs;
                continue;
            }
            assembly[assembly_len++] = b;
            if (assembly_len == 3) {
                assembly_len = 0;
                MsauxPacket packet{};
                if (msaux_parse(assembly[0], assembly[1], assembly[2], packet) &&
                    out != nullptr && done < max) {
                    out[done++] = packet;
                }
            }
        }
        return done;
    }

    hcpp::u64 msaux_byte_count() noexcept { return bytes_seen; }
    hcpp::u64 msaux_resync_count() noexcept { return resyncs; }
    hcpp::u8 msaux_fail_step() noexcept { return fail_step; }

}
