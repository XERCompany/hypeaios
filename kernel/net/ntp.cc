#include "kernel/net/ntp.h"

#include "hcpp/memory.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

namespace hype::net {
    namespace {

        constexpr hcpp::u16 ntp_port_server = 123;
        constexpr hcpp::u16 ntp_port_local = 4123;   // ephemeral bind
        constexpr hcpp::size_t ntp_packet_len = 48;
        constexpr hcpp::u32 ms_per_tick = 10;  // PIT at 100 Hz
        // Seconds between 1900-01-01 (NTP epoch) and 1970-01-01.
        constexpr hcpp::u64 ntp_unix_delta = 2208988800ull;
        // QEMU user-net gateway; slirp has no NTP endpoint, so this is
        // only the graceful-timeout fallback target.
        constexpr hcpp::u32 ip_slirp_gw = 0x0A000202u;  // 10.0.2.2

        bool synced;
        // ntp_unix_base_ms = unix-time-at-sync * 1000 - uptime-at-sync,
        // so net_time_unix() = (uptime_ms() + ntp_offset_ms) / 1000.
        hcpp::u64 ntp_base_ms;
        hcpp::u64 ntp_uptime_ms;

        // Reply state shared with the UDP handler.
        bool reply_done;
        hcpp::u32 reply_secs;

        hcpp::u32 read_be32(const hcpp::u8* p) noexcept {
            return (hcpp::u32(p[0]) << 24) | (hcpp::u32(p[1]) << 16) |
                   (hcpp::u32(p[2]) << 8) | hcpp::u32(p[3]);
        }

        void on_reply(hcpp::u32 src_ip, hcpp::u16 src_port,
                      const hcpp::u8* data,
                      hcpp::size_t length) noexcept {
            (void)src_ip;
            if (data == nullptr || length < ntp_packet_len) return;
            if (src_port != ntp_port_server) return;
            const hcpp::u8 mode = data[0] & 0x07u;
            if (mode != 4 && mode != 5) return;    // server / broadcast
            if (data[1] == 0) return;              // stratum: unsynced
            // Transmit timestamp: seconds at bytes 40..43.
            reply_secs = read_be32(data + 40);
            if (reply_secs < ntp_unix_delta) return;  // pre-1970 = bogus
            reply_done = true;
        }

    }  // namespace

    bool ntp_sync(hcpp::u32 server_ip, hcpp::u32 timeout_ms) noexcept {
        if (!net_ready() || server_ip == 0) return false;
        hcpp::u8 pkt[ntp_packet_len];
        hcpp::set(pkt, 0, sizeof(pkt));
        pkt[0] = 0x1B;  // LI=0, VN=3, Mode=3 (client)
        // Fill the transmit timestamp so servers see a sane origin:
        // uptime seconds is arbitrary but nonzero and monotonic.
        const hcpp::u32 now_s = static_cast<hcpp::u32>(
            time::uptime_ms() / 1000ull + ntp_unix_delta);
        pkt[40] = static_cast<hcpp::u8>(now_s >> 24);
        pkt[41] = static_cast<hcpp::u8>(now_s >> 16);
        pkt[42] = static_cast<hcpp::u8>(now_s >> 8);
        pkt[43] = static_cast<hcpp::u8>(now_s);
        reply_done = false;
        reply_secs = 0;
        (void)net_udp_bind(ntp_port_local, on_reply);
        if (!net_udp_send(server_ip, ntp_port_server, ntp_port_local,
                          pkt, sizeof(pkt)))
            return false;
        const hcpp::u64 limit =
            timeout_ms / ms_per_tick == 0 ? 1 : timeout_ms / ms_per_tick;
        const hcpp::u64 start = interrupts::pit_ticks();
        while (interrupts::pit_ticks() - start < limit) {
            net_poll();
            if (reply_done) break;
            interrupts::pit_wait_ticks(1);
        }
        if (!reply_done) return false;
        ntp_base_ms = hcpp::u64(reply_secs - ntp_unix_delta) * 1000ull;
        ntp_uptime_ms = time::uptime_ms();
        synced = true;
        return true;
    }

    hcpp::u64 net_time_unix() noexcept {
        if (!synced) return 0;
        const hcpp::u64 elapsed = time::uptime_ms() - ntp_uptime_ms;
        return (ntp_base_ms + elapsed) / 1000ull;
    }

    bool ntp_synced() noexcept { return synced; }

    bool ntp_selftest() noexcept {
        hcpp::u32 server = net_ntp_ip();
        if (server == 0) server = ip_slirp_gw;
        const bool ok = ntp_sync(server, 2000);
        if (ok) {
            serial::write("ntp utc=");
            serial::write_dec32(
                static_cast<hcpp::u32>(net_time_unix()));
            serial::write("\r\n");
        } else {
            // QEMU slirp provides no NTP service; a bounded timeout is
            // the honest, expected result.
            serial::write_line("ntp timeout=ok");
        }
        return ok;
    }

}  // namespace hype::net
