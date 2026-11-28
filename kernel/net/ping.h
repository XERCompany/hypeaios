#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // ICMPv4 echo-request client on top of net_icmp_echo_send():
    // registers itself as the echo-reply consumer, sends `count`
    // requests with a bounded net_poll() wait per reply, and prints
    // "ping reply from <ip> seq=N time=~Mms" or "ping timeout seq=N"
    // for each. Returns the number of replies received.
    hcpp::u32 ping(hcpp::u32 ip, hcpp::u32 count = 4) noexcept;

    // Boot selftest: pings the default gateway once and prints the
    // serial marker "ping gw=ok seq=N" (or "ping gw=fail").
    bool net_ping_selftest() noexcept;

}  // namespace hype::net
