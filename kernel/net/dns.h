#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal DNS client (RFC 1035): single-question A-record queries
    // over UDP 53 to the DHCP-learned resolver (option 6), falling
    // back to the gateway. Replies are parsed with a bounded walk that
    // tolerates name-compression pointers; the first A answer wins.
    //
    // dns_resolve() is synchronous: it binds an ephemeral UDP port,
    // sends the query, then drives net_poll() for up to timeout_ms.
    // Prints the serial marker "dns <name>=<a.b.c.d>" on success or
    // "dns <name>=fail" otherwise. Returns false on any failure.
    bool dns_resolve(const char* name, hcpp::u32* ip_out,
                     hcpp::u32 timeout_ms = 3000) noexcept;

    // Boot selftest: resolves "example.com" so the serial log shows a
    // real lookup through the configured resolver.
    bool dns_selftest() noexcept;

}  // namespace hype::net
