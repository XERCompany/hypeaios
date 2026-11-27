#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal IPv4 stack on top of virtio-net: ethernet + ARP (8-entry
    // cache) + ICMP echo reply + UDP sockets-lite + a one-shot DHCP
    // client. Poll-driven: call net_poll() from the idle loop.
    using NetUdpHandler = void (*)(hcpp::u32 src_ip, hcpp::u16 src_port,
                                   const hcpp::u8* data,
                                   hcpp::size_t length) noexcept;

    // Probes the NIC, starts DHCP when present. Prints the serial
    // markers "virtio-net dev=ok mac=.."/"dev=absent", "dhcp ip=..",
    // "arp rx=N", "net stack=ok". Never hangs when the device is absent.
    bool net_initialize() noexcept;
    bool net_ready() noexcept;
    void net_poll() noexcept;

    const hcpp::u8* net_mac() noexcept;
    // Host byte order; 0 when no DHCP lease yet.
    hcpp::u32 net_ip() noexcept;
    hcpp::u32 net_gateway() noexcept;

    bool net_udp_bind(hcpp::u16 port, NetUdpHandler handler) noexcept;
    bool net_udp_send(hcpp::u32 dst_ip, hcpp::u16 dst_port, hcpp::u16 src_port,
                      const void* data, hcpp::size_t length) noexcept;
    // Synchronous ARP resolve with a bounded poll; false on timeout.
    bool net_arp_resolve(hcpp::u32 ip, hcpp::u8 mac_out[6]) noexcept;

}  // namespace hype::net
