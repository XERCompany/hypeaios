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
        constexpr hcpp::u8 cmdbyte_aux_irq = 0x02;
        constexpr hcpp::u8 cmdbyte_aux_clock_dis = 0x20;
        constexpr hcpp::u8 dev_set_defaults = 0xF6;
        constexpr hcpp::u8 dev_set_sample_rate = 0xF3;
        constexpr hcpp::u8 dev_enable_stream = 0xF4;
        constexpr hcpp::u8 dev_get_id = 0xF2;
        constexpr hcpp::u8 dev_request_packet = 0xEB;
        constexpr hcpp::u8 dev_ack = 0xFA;
        constexpr hcpp::u8 intellimouse_id = 0x03;
        constexpr hcpp::u32 io_spin = 100000;
        constexpr hcpp::u8 irq_ring_size = 64;
        // Poll calls (~10 ms each in the idle loop) to wait for the
        // probe packet to arrive via IRQ12 before falling back.
        constexpr hcpp::u32 probe_timeout = 128;

        bool ready;
        hcpp::u64 bytes_seen;
        hcpp::u64 resyncs;
        hcpp::u64 irq_packets;
        hcpp::u8 assembly[4];
        hcpp::u8 assembly_len;
        hcpp::u8 packet_size;
        hcpp::u8 mouse_id;
        hcpp::u8 fail_step;
        MsauxMode mode;
        bool probe_sent;
        hcpp::u32 probe_wait;
        volatile hcpp::u8 irq_ring[irq_ring_size];
        volatile hcpp::u8 irq_head;
        volatile hcpp::u8 irq_tail;

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

        void feed_byte(hcpp::u8 b, bool from_irq, MsauxPacket *out,
                       hcpp::u32 &done, hcpp::u32 max) noexcept {
            ++bytes_seen;
            if (assembly_len == 0) {
                if (b == dev_ack) {
                    // Streaming-mode command ack (e.g. the 0xEB probe);
                    // not packet data.
                    return;
                }
                if ((b & 0x08) == 0) {
                    ++resyncs;
                    return;
                }
            }
            assembly[assembly_len++] = b;
            if (assembly_len == packet_size) {
                assembly_len = 0;
                if (from_irq) {
                    ++irq_packets;
                }
                MsauxPacket packet{};
                if (msaux_parse(assembly[0], assembly[1], assembly[2], packet) &&
                    out != nullptr && done < max) {
                    if (packet_size == 4) {
                        packet.wheel = static_cast<hcpp::i8>(assembly[3]);
                    }
                    out[done++] = packet;
                }
            }
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
            irq_packets = 0;
            assembly_len = 0;
            packet_size = 3;
            mouse_id = 0;
            fail_step = 0;
            mode = MsauxMode::Probing;
            probe_sent = false;
            probe_wait = 0;
            irq_head = 0;
            irq_tail = 0;
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
            // Enable IRQ12 delivery (bit 1) and keep the aux clock
            // enabled (clear bit 5).
            cmdbyte = static_cast<hcpp::u8>((cmdbyte | cmdbyte_aux_irq) &
                                            ~cmdbyte_aux_clock_dis);
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
            // Intellimouse magic sequence: sample rate 200, 100, 80
            // switches a wheel mouse to device ID 0x03 / 4-byte packets.
            if (!aux_send(dev_set_sample_rate, &acked) || !acked) {
                fail_step = 5;
                return false;
            }
            if (!aux_send(200, &acked) || !acked) {
                fail_step = 6;
                return false;
            }
            if (!aux_send(dev_set_sample_rate, &acked) || !acked) {
                fail_step = 7;
                return false;
            }
            if (!aux_send(100, &acked) || !acked) {
                fail_step = 8;
                return false;
            }
            if (!aux_send(dev_set_sample_rate, &acked) || !acked) {
                fail_step = 9;
                return false;
            }
            if (!aux_send(80, &acked) || !acked) {
                fail_step = 10;
                return false;
            }
            if (!aux_send(dev_get_id, &acked) || !acked) {
                fail_step = 11;
                return false;
            }
            if (!wait_output_full()) {
                fail_step = 12;
                return false;
            }
            mouse_id = hype_port_in8(port_data);
            if (mouse_id == intellimouse_id) {
                packet_size = 4;
            } else {
                // Not an Intellimouse: restore the 100/s sample rate.
                if (!aux_send(dev_set_sample_rate, &acked) || !acked) {
                    fail_step = 13;
                    return false;
                }
                if (!aux_send(100, &acked) || !acked) {
                    fail_step = 14;
                    return false;
                }
            }
            if (!aux_send(dev_enable_stream, &acked) || !acked) {
                fail_step = 15;
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

    void msaux_irq() noexcept {
        for (hcpp::u32 i = 0; i < 8; ++i) {
            const hcpp::u8 st = hype_port_in8(port_status);
            if ((st & status_output_full) == 0 ||
                (st & status_aux_data) == 0) {
                return;
            }
            const hcpp::u8 b = hype_port_in8(port_data);
            const hcpp::u8 next = static_cast<hcpp::u8>((irq_head + 1) % irq_ring_size);
            if (next != irq_tail) {
                irq_ring[irq_head] = b;
                irq_head = next;
            }
        }
    }

    hcpp::u32 msaux_poll(MsauxPacket *out, hcpp::u32 max) noexcept {
        hcpp::u32 done = 0;
        if (!ready) {
            return 0;
        }
        if (!probe_sent) {
            probe_sent = true;
            // Request a single packet: the reply arrives as aux data and
            // raises IRQ12, proving IRQ delivery works (or not).
            controller_write(ctl_write_aux);
            data_write(dev_request_packet);
        }
        while (irq_tail != irq_head) {
            const hcpp::u8 b = irq_ring[irq_tail];
            irq_tail = static_cast<hcpp::u8>((irq_tail + 1) % irq_ring_size);
            feed_byte(b, true, out, done, max);
        }
        if (mode == MsauxMode::Probing) {
            if (irq_packets != 0) {
                mode = MsauxMode::Irq;
            } else if (++probe_wait >= probe_timeout) {
                mode = MsauxMode::Poll;
            }
        }
        if (mode != MsauxMode::Poll) {
            return done;
        }
        for (hcpp::u32 guard = 0; guard < 64; ++guard) {
            const hcpp::u8 st = hype_port_in8(port_status);
            if ((st & status_output_full) == 0) {
                break;
            }
            if ((st & status_aux_data) == 0) {
                break;
            }
            feed_byte(hype_port_in8(port_data), false, out, done, max);
        }
        return done;
    }

    MsauxMode msaux_mode() noexcept { return mode; }
    hcpp::u8 msaux_mouse_id() noexcept { return mouse_id; }
    hcpp::u8 msaux_packet_size() noexcept { return packet_size; }
    hcpp::u64 msaux_byte_count() noexcept { return bytes_seen; }
    hcpp::u64 msaux_resync_count() noexcept { return resyncs; }
    hcpp::u8 msaux_fail_step() noexcept { return fail_step; }

}
