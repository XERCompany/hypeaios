#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal SNTP client (RFC 5905): a single 48-byte client request
    // (LI=0/VN=3/Mode=3) over UDP 123. The RTC is treated as
    // read-only hardware — the learned wall clock is kept as an
    // offset against the monotonic uptime instead.
    //
    // ntp_sync() is synchronous with a bounded net_poll() wait of up
    // to timeout_ms; returns false on timeout or malformed replies.
    bool ntp_sync(hcpp::u32 server_ip, hcpp::u32 timeout_ms = 2000) noexcept;

    // Unix time (seconds since 1970) from the last successful sync
    // plus elapsed uptime; 0 when no sync has happened yet.
    hcpp::u64 net_time_unix() noexcept;
    bool ntp_synced() noexcept;

    // Boot selftest: queries the DHCP option-42 NTP server or the
    // QEMU user-net gateway 10.0.2.2 with a 2 s bound. Prints the
    // serial marker "ntp utc=<unix>" on success or "ntp timeout=ok"
    // when the server does not answer (expected under QEMU slirp,
    // which forwards no NTP endpoint).
    bool ntp_selftest() noexcept;

}  // namespace hype::net
