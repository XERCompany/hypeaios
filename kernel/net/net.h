#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal IPv4 stack on top of virtio-net: ethernet + ARP (8-entry
    // cache) + ICMP echo reply + UDP sockets-lite + a one-shot DHCP
    // client. Poll-driven: call net_poll() from the idle loop.
    using NetUdpHandler = void (*)(hcpp::u32 src_ip, hcpp::u16 src_port,
                                   const hcpp::u8* data,
                                   hcpp::size_t length) noexcept;
    // ICMP echo-reply notification: id/seq are already in host byte
    // order; data/length is the echo payload after the 8-byte header.
    using NetIcmpHandler = void (*)(hcpp::u32 src_ip, hcpp::u16 id,
                                    hcpp::u16 seq, const hcpp::u8* data,
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
    // DNS/NTP server learned from DHCP options 6/42 (host byte order,
    // 0 when the server did not advertise them).
    hcpp::u32 net_dns_ip() noexcept;
    hcpp::u32 net_ntp_ip() noexcept;
    // DHCP option-15 domain name (empty string when not advertised).
    const char* net_dns_domain() noexcept;

    bool net_udp_bind(hcpp::u16 port, NetUdpHandler handler) noexcept;
    bool net_udp_send(hcpp::u32 dst_ip, hcpp::u16 dst_port, hcpp::u16 src_port,
                      const void* data, hcpp::size_t length) noexcept;
    // Synchronous ARP resolve with a bounded poll; false on timeout.
    bool net_arp_resolve(hcpp::u32 ip, hcpp::u8 mac_out[6]) noexcept;

    // Sends an ICMPv4 echo request with the given id/seq (host byte
    // order) plus up to 56 bytes of payload. Performs the ARP resolve
    // for the next hop inline; false when the frame could not be sent.
    bool net_icmp_echo_send(hcpp::u32 dst_ip, hcpp::u16 id, hcpp::u16 seq,
                            const hcpp::u8* payload,
                            hcpp::size_t length) noexcept;
    // Registers the echo-reply consumer (ping); pass nullptr to clear.
    void net_icmp_set_handler(NetIcmpHandler handler) noexcept;

}  // namespace hype::net
