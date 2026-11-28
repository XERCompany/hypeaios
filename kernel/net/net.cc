#include "kernel/net/net.h"

#include "hcpp/memory.hpp"
#include "kernel/guard/nfuip.h"
#include "kernel/interrupts/idt.h"
#include "kernel/net/virtio_net.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        constexpr hcpp::u16 eth_type_ipv4 = 0x0800;
        constexpr hcpp::u16 eth_type_arp = 0x0806;

        constexpr hcpp::u8 ip_proto_icmp = 1;
        constexpr hcpp::u8 ip_proto_udp = 17;

        constexpr hcpp::u8 icmp_echo_reply = 0;
        constexpr hcpp::u8 icmp_echo_request = 8;

        constexpr hcpp::u32 ip_broadcast = 0xFFFFFFFFu;

        constexpr hcpp::size_t arp_cache_size = 8;
        constexpr hcpp::size_t udp_bind_max = 8;
        constexpr hcpp::size_t frame_max = 1518;

        constexpr hcpp::u16 dhcp_port_client = 68;
        constexpr hcpp::u16 dhcp_port_server = 67;
        constexpr hcpp::u32 dhcp_magic = 0x63825363;

        // PIT runs at 100 Hz, so one tick is 10 ms.
        constexpr hcpp::u64 dhcp_reply_ticks = 200;  // 2 s per phase
        constexpr hcpp::u64 arp_reply_ticks = 50;    // 0.5 s per attempt
        constexpr hcpp::u32 arp_attempts = 2;
        constexpr hcpp::u32 dhcp_attempts = 3;

        constexpr hcpp::u16 net_htons(hcpp::u16 v) noexcept {
            return static_cast<hcpp::u16>((v >> 8) | (v << 8));
        }
        constexpr hcpp::u32 net_htonl(hcpp::u32 v) noexcept {
            return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
                   (v << 24);
        }
        constexpr hcpp::u16 net_ntohs(hcpp::u16 v) noexcept { return net_htons(v); }
        constexpr hcpp::u32 net_ntohl(hcpp::u32 v) noexcept { return net_htonl(v); }

        struct EthHdr {
            hcpp::u8 dst[6];
            hcpp::u8 src[6];
            hcpp::u16 type;
        } __attribute__((packed));

        struct ArpPacket {
            hcpp::u16 htype;
            hcpp::u16 ptype;
            hcpp::u8 hlen;
            hcpp::u8 plen;
            hcpp::u16 op;
            hcpp::u8 sha[6];
            hcpp::u32 spa;
            hcpp::u8 tha[6];
            hcpp::u32 tpa;
        } __attribute__((packed));

        struct Ipv4Hdr {
            hcpp::u8 ihl_ver;
            hcpp::u8 tos;
            hcpp::u16 total_len;
            hcpp::u16 id;
            hcpp::u16 frag;
            hcpp::u8 ttl;
            hcpp::u8 proto;
            hcpp::u16 csum;
            hcpp::u32 src;
            hcpp::u32 dst;
        } __attribute__((packed));

        struct IcmpHdr {
            hcpp::u8 type;
            hcpp::u8 code;
            hcpp::u16 csum;
            hcpp::u16 id;
            hcpp::u16 seq;
        } __attribute__((packed));

        struct UdpHdr {
            hcpp::u16 src_port;
            hcpp::u16 dst_port;
            hcpp::u16 len;
            hcpp::u16 csum;
        } __attribute__((packed));

        struct DhcpPacket {
            hcpp::u8 op;
            hcpp::u8 htype;
            hcpp::u8 hlen;
            hcpp::u8 hops;
            hcpp::u32 xid;
            hcpp::u16 secs;
            hcpp::u16 flags;
            hcpp::u32 ciaddr;
            hcpp::u32 yiaddr;
            hcpp::u32 siaddr;
            hcpp::u32 giaddr;
            hcpp::u8 chaddr[16];
            hcpp::u8 sname[64];
            hcpp::u8 file[128];
            hcpp::u32 magic;
            hcpp::u8 options[312];
        } __attribute__((packed));

        struct ArpEntry {
            hcpp::u32 ip;
            hcpp::u8 mac[6];
            bool valid;
        };

        struct UdpBind {
            hcpp::u16 port;
            NetUdpHandler handler;
        };

        bool ready;
        bool have_ip;
        hcpp::u8 our_mac[6];
        hcpp::u32 our_ip;       // host byte order
        hcpp::u32 gateway_ip;   // host byte order
        hcpp::u32 subnet_mask;  // host byte order
        hcpp::u16 ip_id;
        hcpp::u32 arp_rx_count;
        hcpp::u32 stat_tx;  // frames handed to the NIC
        hcpp::u32 stat_rx;  // frames pulled from the RX ring
        ArpEntry arp_cache[arp_cache_size];
        UdpBind udp_binds[udp_bind_max];
        hcpp::u8 rx_frame[frame_max];
        hcpp::u8 tx_frame[frame_max + 64];

        // DHCP transaction state: the last parsed offer/ack for the xid
        // we generated, filled by the UDP port-68 handler.
        bool dhcp_listening;
        hcpp::u32 dhcp_xid;
        hcpp::u8 dhcp_msg_seen;  // last DHCP message-type option value
        hcpp::u32 dhcp_offer_ip;
        hcpp::u32 dhcp_server_id;
        hcpp::u32 dhcp_router;
        hcpp::u32 dhcp_subnet;
        // DHCP option 6/42/15 captures: DNS + NTP server addresses and
        // the domain name, taken from the last offer/ack seen.
        hcpp::u32 dhcp_dns;
        hcpp::u32 dhcp_ntp;
        char dhcp_domain[64];

        // ping consumer for ICMP echo replies (kernel/net/ping.cc).
        NetIcmpHandler icmp_handler;

        constexpr hcpp::size_t dhcp_options_offset =
            sizeof(DhcpPacket) - 312;  // fixed BOOTP header + magic cookie

        constexpr hcpp::u32 read_be32(const hcpp::u8* p) noexcept {
            return (hcpp::u32(p[0]) << 24) | (hcpp::u32(p[1]) << 16) |
                   (hcpp::u32(p[2]) << 8) | hcpp::u32(p[3]);
        }

        hcpp::u16 inet_checksum(const void* data, hcpp::size_t bytes) noexcept {
            const hcpp::u8* p = static_cast<const hcpp::u8*>(data);
            hcpp::u32 sum = 0;
            while (bytes > 1) {
                sum += static_cast<hcpp::u16>((p[0] << 8) | p[1]);
                p += 2;
                bytes -= 2;
            }
            if (bytes != 0) sum += static_cast<hcpp::u16>(p[0] << 8);
            while ((sum >> 16) != 0) sum = (sum & 0xFFFFu) + (sum >> 16);
            return static_cast<hcpp::u16>(~sum & 0xFFFFu);
        }

        bool mac_equal(const hcpp::u8* a, const hcpp::u8* b) noexcept {
            for (hcpp::u8 i = 0; i < 6; ++i)
                if (a[i] != b[i]) return false;
            return true;
        }

        void mac_broadcast(hcpp::u8 out[6]) noexcept {
            for (hcpp::u8 i = 0; i < 6; ++i) out[i] = 0xFF;
        }

        void write_ip(hcpp::u32 ip) noexcept {  // host byte order
            for (hcpp::u8 i = 0; i < 4; ++i) {
                serial::write_dec32((ip >> (24 - i * 8)) & 0xFFu);
                if (i != 3) serial::write_char('.');
            }
        }

        void arp_cache_store(hcpp::u32 ip, const hcpp::u8* mac) noexcept {
            if (ip == 0) return;
            ArpEntry* slot = nullptr;
            for (hcpp::size_t i = 0; i < arp_cache_size; ++i) {
                if (arp_cache[i].valid && arp_cache[i].ip == ip) {
                    slot = &arp_cache[i];
                    break;
                }
                if (slot == nullptr && !arp_cache[i].valid) slot = &arp_cache[i];
            }
            if (slot == nullptr) slot = &arp_cache[0];  // simple eviction
            slot->ip = ip;
            hcpp::copy(slot->mac, mac, 6);
            slot->valid = true;
        }

        bool arp_cache_lookup(hcpp::u32 ip, hcpp::u8 mac_out[6]) noexcept {
            for (hcpp::size_t i = 0; i < arp_cache_size; ++i) {
                if (arp_cache[i].valid && arp_cache[i].ip == ip) {
                    hcpp::copy(mac_out, arp_cache[i].mac, 6);
                    return true;
                }
            }
            return false;
        }

        bool eth_send(const hcpp::u8 dst_mac[6], hcpp::u16 type,
                      const void* payload, hcpp::size_t length) noexcept {
            if (sizeof(EthHdr) + length > sizeof(tx_frame)) return false;
            EthHdr* eth = reinterpret_cast<EthHdr*>(tx_frame);
            hcpp::copy(eth->dst, dst_mac, 6);
            hcpp::copy(eth->src, our_mac, 6);
            eth->type = net_htons(type);
            hcpp::copy(tx_frame + sizeof(EthHdr), payload, length);
            const bool sent =
                virtio_net_send(tx_frame, sizeof(EthHdr) + length);
            if (sent) ++stat_tx;
            return sent;
        }

        void arp_send_request(hcpp::u32 target_ip) noexcept {
            ArpPacket req{};
            req.htype = net_htons(1);
            req.ptype = net_htons(eth_type_ipv4);
            req.hlen = 6;
            req.plen = 4;
            req.op = net_htons(1);
            hcpp::copy(req.sha, our_mac, 6);
            req.spa = net_htonl(our_ip);
            hcpp::set(req.tha, 0, 6);
            req.tpa = net_htonl(target_ip);
            hcpp::u8 bcast[6];
            mac_broadcast(bcast);
            eth_send(bcast, eth_type_arp, &req, sizeof(req));
        }

        void arp_send_reply(const hcpp::u8 dst_mac[6], hcpp::u32 dst_ip) noexcept {
            ArpPacket rep{};
            rep.htype = net_htons(1);
            rep.ptype = net_htons(eth_type_ipv4);
            rep.hlen = 6;
            rep.plen = 4;
            rep.op = net_htons(2);
            hcpp::copy(rep.sha, our_mac, 6);
            rep.spa = net_htonl(our_ip);
            hcpp::copy(rep.tha, dst_mac, 6);
            rep.tpa = net_htonl(dst_ip);
            eth_send(dst_mac, eth_type_arp, &rep, sizeof(rep));
        }

        void handle_arp(const hcpp::u8* data, hcpp::size_t length) noexcept {
            if (length < sizeof(ArpPacket)) return;
            const ArpPacket* arp = reinterpret_cast<const ArpPacket*>(data);
            if (arp->hlen != 6 || arp->plen != 4 ||
                net_ntohs(arp->ptype) != eth_type_ipv4)
                return;
            ++arp_rx_count;
            serial::write("arp rx=");
            serial::write_dec32(arp_rx_count);
            serial::write("\r\n");
            const hcpp::u32 sender_ip = net_ntohl(arp->spa);
            const hcpp::u32 target_ip = net_ntohl(arp->tpa);
            arp_cache_store(sender_ip, arp->sha);
            // Answer anyone asking for our address; requests carry a zero
            // target MAC and are broadcast.
            if (net_ntohs(arp->op) == 1 && have_ip && target_ip == our_ip)
                arp_send_reply(arp->sha, sender_ip);
        }

        void icmp_reply(const hcpp::u8 src_mac[6], const Ipv4Hdr* ip,
                        const hcpp::u8* payload, hcpp::size_t length) noexcept {
            if (length < sizeof(IcmpHdr) ||
                sizeof(Ipv4Hdr) + length > frame_max)
                return;
            Ipv4Hdr reply_ip{};
            reply_ip.ihl_ver = 0x45;
            reply_ip.tos = 0;
            reply_ip.total_len =
                net_htons(static_cast<hcpp::u16>(sizeof(Ipv4Hdr) + length));
            reply_ip.id = net_htons(++ip_id);
            reply_ip.frag = 0;
            reply_ip.ttl = 64;
            reply_ip.proto = ip_proto_icmp;
            reply_ip.csum = 0;
            reply_ip.src = net_htonl(our_ip);
            reply_ip.dst = ip->src;
            reply_ip.csum = net_htons(inet_checksum(&reply_ip, sizeof(reply_ip)));

            IcmpHdr icmp{};
            icmp.type = icmp_echo_reply;
            icmp.code = 0;
            icmp.csum = 0;
            icmp.id = reinterpret_cast<const IcmpHdr*>(payload)->id;
            icmp.seq = reinterpret_cast<const IcmpHdr*>(payload)->seq;
            hcpp::copy(tx_frame + sizeof(EthHdr) + sizeof(Ipv4Hdr), &reply_ip,
                       sizeof(reply_ip));
            hcpp::copy(tx_frame + sizeof(EthHdr) + sizeof(Ipv4Hdr) +
                           sizeof(IcmpHdr),
                       payload + sizeof(IcmpHdr), length - sizeof(IcmpHdr));
            IcmpHdr* out_icmp = reinterpret_cast<IcmpHdr*>(
                tx_frame + sizeof(EthHdr) + sizeof(Ipv4Hdr));
            *out_icmp = icmp;
            out_icmp->csum = net_htons(inet_checksum(out_icmp, length));

            EthHdr* eth = reinterpret_cast<EthHdr*>(tx_frame);
            hcpp::copy(eth->dst, src_mac, 6);
            hcpp::copy(eth->src, our_mac, 6);
            eth->type = net_htons(eth_type_ipv4);
            if (virtio_net_send(tx_frame,
                                sizeof(EthHdr) + sizeof(Ipv4Hdr) + length))
                ++stat_tx;
        }

        // DHCP option walk; returns the first option of `code` found.
        const hcpp::u8* dhcp_option(const DhcpPacket& pkt, hcpp::size_t length,
                                    hcpp::u8 code) noexcept {
            if (length < dhcp_options_offset) return nullptr;
            const hcpp::size_t opt_len = length - dhcp_options_offset;
            const hcpp::u8* opt = pkt.options;
            hcpp::size_t i = 0;
            while (i < opt_len) {
                const hcpp::u8 kind = opt[i];
                if (kind == 255) return nullptr;
                if (kind == 0) {
                    ++i;
                    continue;
                }
                if (i + 1 >= opt_len) return nullptr;
                const hcpp::u8 len = opt[i + 1];
                if (i + 2 + len > opt_len) return nullptr;
                if (kind == code) return opt + i;
                i += 2 + len;
            }
            return nullptr;
        }

        void handle_dhcp(const hcpp::u8* data, hcpp::size_t length) noexcept {
            // A DHCP reply is the fixed BOOTP header + magic cookie plus a
            // *variable-length* options area; servers are not required to
            // pad it out to DhcpPacket::options capacity (QEMU's user-net
            // DHCP replies arrive short). dhcp_option() already bounds its
            // walk by `length`, so the fixed header is all we must require.
            if (!dhcp_listening || length < dhcp_options_offset) return;
            const DhcpPacket* pkt = reinterpret_cast<const DhcpPacket*>(data);
            if (pkt->op != 2 || pkt->xid != dhcp_xid ||
                net_ntohl(pkt->magic) != dhcp_magic)
                return;
            const hcpp::u8* type = dhcp_option(*pkt, length, 53);
            if (type == nullptr || type[1] != 1) return;
            const hcpp::u8 msg = type[2];
            if (msg != 2 && msg != 5) return;  // offer / ack only
            dhcp_msg_seen = msg;
            dhcp_offer_ip = net_ntohl(pkt->yiaddr);
            dhcp_server_id = net_ntohl(pkt->siaddr);
            dhcp_router = 0;
            dhcp_subnet = 0xFFFFFF00u;
            dhcp_dns = 0;
            dhcp_ntp = 0;
            dhcp_domain[0] = '\0';
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 54);
                opt != nullptr && opt[1] >= 4)
                dhcp_server_id = read_be32(opt + 2);
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 3);
                opt != nullptr && opt[1] >= 4)
                dhcp_router = read_be32(opt + 2);
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 1);
                opt != nullptr && opt[1] >= 4)
                dhcp_subnet = read_be32(opt + 2);
            // Option 6 may carry several DNS servers; the first is
            // enough for the resolver. Option 42 is the NTP server
            // list (same layout), option 15 the domain name string.
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 6);
                opt != nullptr && opt[1] >= 4)
                dhcp_dns = read_be32(opt + 2);
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 42);
                opt != nullptr && opt[1] >= 4)
                dhcp_ntp = read_be32(opt + 2);
            if (const hcpp::u8* opt = dhcp_option(*pkt, length, 15);
                opt != nullptr && opt[1] > 0) {
                const hcpp::u8 n = opt[1] < sizeof(dhcp_domain) - 1
                                       ? opt[1]
                                       : sizeof(dhcp_domain) - 1;
                hcpp::copy(dhcp_domain, opt + 2, n);
                dhcp_domain[n] = '\0';
            }
        }

        void handle_udp(const hcpp::u8* data, hcpp::size_t length,
                        hcpp::u32 src_ip) noexcept {
            if (length < sizeof(UdpHdr)) return;
            const UdpHdr* udp = reinterpret_cast<const UdpHdr*>(data);
            const hcpp::u16 dst_port = net_ntohs(udp->dst_port);
            const hcpp::u16 src_port = net_ntohs(udp->src_port);
            const hcpp::u16 udp_len = net_ntohs(udp->len);
            hcpp::size_t payload_len =
                udp_len >= sizeof(UdpHdr) ? udp_len - sizeof(UdpHdr) : 0;
            if (payload_len > length - sizeof(UdpHdr))
                payload_len = length - sizeof(UdpHdr);
            if (dst_port == dhcp_port_client) {
                handle_dhcp(data + sizeof(UdpHdr), payload_len);
                return;
            }
            for (hcpp::size_t i = 0; i < udp_bind_max; ++i) {
                if (udp_binds[i].port == dst_port &&
                    udp_binds[i].handler != nullptr) {
                    udp_binds[i].handler(src_ip, src_port,
                                         data + sizeof(UdpHdr), payload_len);
                    return;
                }
            }
        }

        void handle_ipv4(const hcpp::u8 src_mac[6], const hcpp::u8* data,
                         hcpp::size_t length) noexcept {
            if (length < sizeof(Ipv4Hdr)) return;
            const Ipv4Hdr* ip = reinterpret_cast<const Ipv4Hdr*>(data);
            const hcpp::u8 ihl = (ip->ihl_ver & 0x0F) * 4;
            if ((ip->ihl_ver >> 4) != 4 || ihl < sizeof(Ipv4Hdr) || ihl > length)
                return;
            const hcpp::u32 dst = net_ntohl(ip->dst);
            if (have_ip && dst != our_ip && dst != ip_broadcast &&
                dst != (our_ip | ~subnet_mask))
                return;
            if (!have_ip && dst != ip_broadcast) return;  // only DHCP arrives
            const hcpp::u16 total = net_ntohs(ip->total_len);
            if (total < ihl) return;
            const hcpp::size_t ip_len =
                total <= length ? total : length;
            const hcpp::u8* payload = data + ihl;
            const hcpp::size_t payload_len = ip_len - ihl;
            const hcpp::u32 src_ip = net_ntohl(ip->src);
            if (ip->proto == ip_proto_icmp && have_ip) {
                const IcmpHdr* icmp =
                    reinterpret_cast<const IcmpHdr*>(payload);
                if (payload_len < sizeof(IcmpHdr)) return;
                if (icmp->type == icmp_echo_request) {
                    icmp_reply(src_mac, ip, payload, payload_len);
                } else if (icmp->type == icmp_echo_reply &&
                           icmp_handler != nullptr) {
                    icmp_handler(src_ip, net_ntohs(icmp->id),
                                 net_ntohs(icmp->seq),
                                 payload + sizeof(IcmpHdr),
                                 payload_len - sizeof(IcmpHdr));
                }
            } else if (ip->proto == ip_proto_udp) {
                handle_udp(payload, payload_len, src_ip);
            }
        }

        void handle_frame(const hcpp::u8* data, hcpp::size_t length) noexcept {
            if (length < sizeof(EthHdr)) return;
            const EthHdr* eth = reinterpret_cast<const EthHdr*>(data);
            const hcpp::u16 type = net_ntohs(eth->type);
            const bool for_us = mac_equal(eth->dst, our_mac) ||
                                (eth->dst[0] & 1) != 0;  // broadcast/multicast
            if (!for_us) return;
            if (type == eth_type_arp) {
                handle_arp(data + sizeof(EthHdr), length - sizeof(EthHdr));
            } else if (type == eth_type_ipv4) {
                handle_ipv4(eth->src, data + sizeof(EthHdr),
                            length - sizeof(EthHdr));
            }
        }

        bool arp_resolve_impl(hcpp::u32 ip, hcpp::u8 mac_out[6]) noexcept {
            if (arp_cache_lookup(ip, mac_out)) return true;
            for (hcpp::u32 attempt = 0; attempt < arp_attempts; ++attempt) {
                arp_send_request(ip);
                const hcpp::u64 start = interrupts::pit_ticks();
                while (interrupts::pit_ticks() - start < arp_reply_ticks) {
                    net_poll();
                    if (arp_cache_lookup(ip, mac_out)) return true;
                    interrupts::pit_wait_ticks(1);
                }
            }
            return false;
        }

        bool same_subnet(hcpp::u32 ip) noexcept {
            return have_ip && (ip & subnet_mask) == (our_ip & subnet_mask);
        }

        bool ipv4_send(hcpp::u32 dst_ip, hcpp::u8 proto,
                       const hcpp::u8 src_mac_hint[6], const void* payload,
                       hcpp::size_t payload_len, hcpp::u16 src_port,
                       hcpp::u16 dst_port) noexcept {
            // Builds UDP- or ICMP-over-IPv4 into tx_frame; src/dst
            // ports are only meaningful for UDP. For ICMP the payload
            // is a complete ICMP message (checksum already computed).
            if (proto != ip_proto_udp && proto != ip_proto_icmp)
                return false;
            const hcpp::size_t l4_hdr =
                proto == ip_proto_udp ? sizeof(UdpHdr) : 0;
            if (sizeof(EthHdr) + sizeof(Ipv4Hdr) + l4_hdr + payload_len >
                sizeof(tx_frame))
                return false;
            hcpp::u8 dst_mac[6];
            if (dst_ip == ip_broadcast ||
                (have_ip && dst_ip == (our_ip | ~subnet_mask))) {
                mac_broadcast(dst_mac);
            } else if (src_mac_hint != nullptr) {
                hcpp::copy(dst_mac, src_mac_hint, 6);
            } else {
                const hcpp::u32 next_hop =
                    same_subnet(dst_ip) ? dst_ip : gateway_ip;
                if (next_hop == 0 ||
                    !arp_resolve_impl(next_hop, dst_mac))
                    return false;
            }

            EthHdr* eth = reinterpret_cast<EthHdr*>(tx_frame);
            hcpp::copy(eth->dst, dst_mac, 6);
            hcpp::copy(eth->src, our_mac, 6);
            eth->type = net_htons(eth_type_ipv4);

            Ipv4Hdr* ip = reinterpret_cast<Ipv4Hdr*>(tx_frame + sizeof(EthHdr));
            hcpp::set(ip, 0, sizeof(Ipv4Hdr));
            ip->ihl_ver = 0x45;
            ip->total_len = net_htons(static_cast<hcpp::u16>(
                sizeof(Ipv4Hdr) + l4_hdr + payload_len));
            ip->id = net_htons(++ip_id);
            ip->ttl = 64;
            ip->proto = proto;
            ip->src = net_htonl(our_ip);
            ip->dst = net_htonl(dst_ip);
            ip->csum = net_htons(inet_checksum(ip, sizeof(Ipv4Hdr)));

            hcpp::u8* l4 = tx_frame + sizeof(EthHdr) + sizeof(Ipv4Hdr);
            if (proto == ip_proto_udp) {
                UdpHdr* udp = reinterpret_cast<UdpHdr*>(l4);
                udp->src_port = net_htons(src_port);
                udp->dst_port = net_htons(dst_port);
                udp->len = net_htons(
                    static_cast<hcpp::u16>(sizeof(UdpHdr) + payload_len));
                udp->csum = 0;  // UDP checksum optional over IPv4
            }
            hcpp::copy(l4 + l4_hdr, payload, payload_len);
            const bool sent = virtio_net_send(
                tx_frame, sizeof(EthHdr) + sizeof(Ipv4Hdr) + l4_hdr +
                              payload_len);
            if (sent) ++stat_tx;
            return sent;
        }

        void dhcp_send(hcpp::u8 msg_type, hcpp::u32 requested_ip,
                       hcpp::u32 server_id) noexcept {
            DhcpPacket pkt{};
            pkt.op = 1;
            pkt.htype = 1;
            pkt.hlen = 6;
            pkt.xid = dhcp_xid;
            pkt.flags = net_htons(0x8000);  // broadcast reply
            hcpp::copy(pkt.chaddr, our_mac, 6);
            pkt.magic = net_htonl(dhcp_magic);
            hcpp::size_t i = 0;
            pkt.options[i++] = 53;
            pkt.options[i++] = 1;
            pkt.options[i++] = msg_type;
            if (requested_ip != 0) {
                pkt.options[i++] = 50;
                pkt.options[i++] = 4;
                const hcpp::u32 be = net_htonl(requested_ip);
                hcpp::copy(pkt.options + i, &be, 4);
                i += 4;
            }
            if (server_id != 0) {
                pkt.options[i++] = 54;
                pkt.options[i++] = 4;
                const hcpp::u32 be = net_htonl(server_id);
                hcpp::copy(pkt.options + i, &be, 4);
                i += 4;
            }
            pkt.options[i++] = 55;  // parameter request list
            pkt.options[i++] = 6;
            pkt.options[i++] = 1;   // subnet mask
            pkt.options[i++] = 3;   // router
            pkt.options[i++] = 6;   // dns
            pkt.options[i++] = 51;  // lease time
            pkt.options[i++] = 42;  // ntp servers
            pkt.options[i++] = 15;  // domain name
            pkt.options[i++] = 255;
            ipv4_send(ip_broadcast, ip_proto_udp, nullptr, &pkt,
                      sizeof(DhcpPacket), dhcp_port_client, dhcp_port_server);
        }

        bool dhcp_wait(hcpp::u8 want_msg) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            while (interrupts::pit_ticks() - start < dhcp_reply_ticks) {
                net_poll();
                if (dhcp_msg_seen == want_msg) return true;
                interrupts::pit_wait_ticks(1);
            }
            return false;
        }

        bool dhcp_acquire() noexcept {
            dhcp_listening = true;
            dhcp_xid = 0x48595045u ^
                       static_cast<hcpp::u32>(interrupts::pit_ticks());
            for (hcpp::u32 attempt = 0; attempt < dhcp_attempts; ++attempt) {
                dhcp_msg_seen = 0;
                dhcp_send(1, 0, 0);  // discover
                if (!dhcp_wait(2)) continue;
                const hcpp::u32 offered = dhcp_offer_ip;
                const hcpp::u32 server = dhcp_server_id;
                dhcp_msg_seen = 0;
                dhcp_send(3, offered, server);  // request
                if (!dhcp_wait(5)) continue;
                our_ip = dhcp_offer_ip;
                gateway_ip = dhcp_router;
                subnet_mask = dhcp_subnet;
                have_ip = our_ip != 0;
                dhcp_listening = false;
                serial::write("dhcp ip=");
                write_ip(our_ip);
                serial::write(" gw=");
                write_ip(gateway_ip);
                serial::write("\r\n");
                serial::write("net dns=");
                write_ip(dhcp_dns);
                serial::write(" ntp=");
                write_ip(dhcp_ntp);
                serial::write("\r\n");
                return true;
            }
            dhcp_listening = false;
            serial::write_line("dhcp ip=none");
            return false;
        }

    }  // namespace

    bool net_initialize() noexcept {
        ready = false;
        have_ip = false;
        if (!virtio_net_probe()) return false;
        hcpp::copy(our_mac, virtio_net_mac(), 6);
        for (auto& e : arp_cache) e.valid = false;
        for (auto& b : udp_binds) b = {};
        ip_id = 0;
        arp_rx_count = 0;
        stat_tx = 0;
        stat_rx = 0;
        dhcp_dns = 0;
        dhcp_ntp = 0;
        dhcp_domain[0] = '\0';
        icmp_handler = nullptr;
        dhcp_acquire();
        // Warm the ARP cache with the gateway so the "arp rx" marker
        // fires even on an otherwise quiet network.
        hcpp::u8 mac_out[6];
        if (have_ip && gateway_ip != 0) (void)arp_resolve_impl(gateway_ip, mac_out);
        ready = true;
        serial::write("net tx=");
        serial::write_dec32(stat_tx);
        serial::write(" rx=");
        serial::write_dec32(stat_rx);
        serial::write("\r\n");
        serial::write_line("net stack=ok");
        return true;
    }

    bool net_ready() noexcept { return ready; }

    void net_poll() noexcept {
        if (!virtio_net_ready()) return;
        for (hcpp::u32 i = 0; i < 16; ++i) {
            const hcpp::size_t n = virtio_net_recv(rx_frame, sizeof(rx_frame));
            if (n == 0) break;
            ++stat_rx;
            handle_frame(rx_frame, n);
        }
    }

    const hcpp::u8* net_mac() noexcept { return our_mac; }
    hcpp::u32 net_ip() noexcept { return our_ip; }
    hcpp::u32 net_gateway() noexcept { return gateway_ip; }
    hcpp::u32 net_dns_ip() noexcept { return dhcp_dns; }
    hcpp::u32 net_ntp_ip() noexcept { return dhcp_ntp; }
    const char* net_dns_domain() noexcept { return dhcp_domain; }

    bool net_udp_bind(hcpp::u16 port, NetUdpHandler handler) noexcept {
        if (handler == nullptr) return false;
        // HypeGuard net gate (issue #114): the nfuip policy can deny a
        // bind for a non-system subject; Deny fails the call outright.
        if (hype::guard::nfuip_check(hype::guard::nfuip_op_udp_bind, port) ==
            hype::guard::GuardResult::Deny) {
            return false;
        }
        for (hcpp::size_t i = 0; i < udp_bind_max; ++i) {
            if (udp_binds[i].handler == nullptr) {
                udp_binds[i].port = port;
                udp_binds[i].handler = handler;
                return true;
            }
            if (udp_binds[i].port == port) {
                udp_binds[i].handler = handler;
                return true;
            }
        }
        return false;
    }

    bool net_udp_send(hcpp::u32 dst_ip, hcpp::u16 dst_port, hcpp::u16 src_port,
                      const void* data, hcpp::size_t length) noexcept {
        if (!ready || data == nullptr || length == 0) return false;
        // HypeGuard net gate (issue #114): sends are mediated on the
        // destination port ("net:udp:send:<port>" objects).
        if (hype::guard::nfuip_check(hype::guard::nfuip_op_udp_send, dst_port) ==
            hype::guard::GuardResult::Deny) {
            return false;
        }
        return ipv4_send(dst_ip, ip_proto_udp, nullptr, data, length, src_port,
                         dst_port);
    }

    bool net_arp_resolve(hcpp::u32 ip, hcpp::u8 mac_out[6]) noexcept {
        if (!ready || mac_out == nullptr) return false;
        return arp_resolve_impl(ip, mac_out);
    }

    bool net_icmp_echo_send(hcpp::u32 dst_ip, hcpp::u16 id, hcpp::u16 seq,
                            const hcpp::u8* payload,
                            hcpp::size_t length) noexcept {
        if (!ready || !have_ip || length > 56) return false;
        // The ICMP message (header + payload) is built in a small stack
        // buffer because ipv4_send() assembles the frame in-place.
        hcpp::u8 msg[sizeof(IcmpHdr) + 56];
        IcmpHdr* icmp = reinterpret_cast<IcmpHdr*>(msg);
        icmp->type = icmp_echo_request;
        icmp->code = 0;
        icmp->csum = 0;
        icmp->id = net_htons(id);
        icmp->seq = net_htons(seq);
        if (payload != nullptr && length != 0)
            hcpp::copy(msg + sizeof(IcmpHdr), payload, length);
        icmp->csum =
            net_htons(inet_checksum(msg, sizeof(IcmpHdr) + length));
        return ipv4_send(dst_ip, ip_proto_icmp, nullptr, msg,
                         sizeof(IcmpHdr) + length, 0, 0);
    }

    void net_icmp_set_handler(NetIcmpHandler handler) noexcept {
        icmp_handler = handler;
    }

}  // namespace hype::net
