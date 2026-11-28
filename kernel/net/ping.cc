#include "kernel/net/ping.h"

#include "hcpp/memory.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        // PIT runs at 100 Hz: one tick is 10 ms.
        constexpr hcpp::u32 ms_per_tick = 10;
        constexpr hcpp::u32 reply_wait_ticks = 100;  // 1 s per echo
        constexpr hcpp::u32 ping_payload_len = 32;

        void write_ip(hcpp::u32 ip) noexcept {
            for (hcpp::u8 i = 0; i < 4; ++i) {
                serial::write_dec32((ip >> (24 - i * 8)) & 0xFFu);
                if (i != 3) serial::write_char('.');
            }
        }

        // Pending-echo state shared with the ICMP reply handler. A
        // single outstanding echo is enough for a synchronous ping.
        hcpp::u16 pending_id;
        hcpp::u16 pending_seq;
        bool pending_done;
        hcpp::u32 pending_rtt_ticks;

        void on_echo_reply(hcpp::u32 src_ip, hcpp::u16 id, hcpp::u16 seq,
                           const hcpp::u8* data,
                           hcpp::size_t length) noexcept {
            (void)src_ip;
            (void)data;
            (void)length;
            if (id != pending_id || seq != pending_seq) return;
            pending_done = true;
        }

    }  // namespace

    hcpp::u32 ping(hcpp::u32 ip, hcpp::u32 count) noexcept {
        hcpp::u32 replies = 0;
        if (!net_ready() || ip == 0 || count == 0) return 0;
        net_icmp_set_handler(on_echo_reply);
        const hcpp::u16 id = static_cast<hcpp::u16>(
            (interrupts::pit_ticks() ^ 0x5050u) & 0xFFFFu);
        hcpp::u8 payload[ping_payload_len];
        for (hcpp::u32 i = 0; i < ping_payload_len; ++i)
            payload[i] = static_cast<hcpp::u8>(i + 0x10u);
        for (hcpp::u32 seq = 1; seq <= count; ++seq) {
            pending_id = id;
            pending_seq = static_cast<hcpp::u16>(seq);
            pending_done = false;
            const hcpp::u64 sent_at = interrupts::pit_ticks();
            if (!net_icmp_echo_send(ip, id, pending_seq, payload,
                                    ping_payload_len)) {
                serial::write("ping timeout seq=");
                serial::write_dec32(seq);
                serial::write("\r\n");
                continue;
            }
            while (interrupts::pit_ticks() - sent_at < reply_wait_ticks &&
                   !pending_done) {
                net_poll();
                interrupts::pit_wait_ticks(1);
            }
            if (pending_done) {
                pending_rtt_ticks = static_cast<hcpp::u32>(
                    interrupts::pit_ticks() - sent_at);
                ++replies;
                serial::write("ping reply from ");
                write_ip(ip);
                serial::write(" seq=");
                serial::write_dec32(seq);
                serial::write(" time=~");
                serial::write_dec32(pending_rtt_ticks * ms_per_tick);
                serial::write("ms\r\n");
            } else {
                serial::write("ping timeout seq=");
                serial::write_dec32(seq);
                serial::write("\r\n");
            }
        }
        net_icmp_set_handler(nullptr);
        return replies;
    }

    bool net_ping_selftest() noexcept {
        const hcpp::u32 gw = net_gateway();
        if (gw == 0) {
            serial::write_line("ping gw=fail");
            return false;
        }
        const hcpp::u32 replies = ping(gw, 1);
        serial::write("ping gw=");
        serial::write(replies != 0 ? "ok" : "fail");
        serial::write(" seq=1\r\n");
        return replies != 0;
    }

}  // namespace hype::net
