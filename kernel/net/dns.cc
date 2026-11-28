#include "kernel/net/dns.h"

#include "hcpp/memory.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        constexpr hcpp::u16 dns_port_server = 53;
        constexpr hcpp::u16 dns_port_local = 4953;   // ephemeral bind
        constexpr hcpp::size_t dns_query_max = 320;  // hdr + name + tail
        constexpr hcpp::u32 ip_slirp_dns = 0x0A000203u;  // QEMU 10.0.2.3

        // PIT runs at 100 Hz: one tick is 10 ms.
        constexpr hcpp::u32 ms_per_tick = 10;

        struct DnsHdr {
            hcpp::u16 id;
            hcpp::u16 flags;
            hcpp::u16 qdcount;
            hcpp::u16 ancount;
            hcpp::u16 nscount;
            hcpp::u16 arcount;
        } __attribute__((packed));

        constexpr hcpp::u16 dns_htons(hcpp::u16 v) noexcept {
            return static_cast<hcpp::u16>((v >> 8) | (v << 8));
        }
        constexpr hcpp::u16 dns_ntohs(hcpp::u16 v) noexcept {
            return dns_htons(v);
        }
        constexpr hcpp::u32 dns_be32(const hcpp::u8* p) noexcept {
            return (hcpp::u32(p[0]) << 24) | (hcpp::u32(p[1]) << 16) |
                   (hcpp::u32(p[2]) << 8) | hcpp::u32(p[3]);
        }

        void write_ip(hcpp::u32 ip) noexcept {
            for (hcpp::u8 i = 0; i < 4; ++i) {
                serial::write_dec32((ip >> (24 - i * 8)) & 0xFFu);
                if (i != 3) serial::write_char('.');
            }
        }

        // Transaction state shared with the UDP reply handler.
        hcpp::u16 query_id;
        bool reply_done;
        hcpp::u32 reply_addr;

        // Skips one (possibly compressed) domain name at *off; false
        // when the walk would leave the message bounds. Compression
        // pointers terminate the in-place walk after their 2 bytes.
        bool skip_name(const hcpp::u8* msg, hcpp::size_t len,
                       hcpp::size_t* off) noexcept {
            for (hcpp::u32 hops = 0; hops < 128; ++hops) {
                if (*off >= len) return false;
                const hcpp::u8 l = msg[*off];
                if (l == 0) {
                    ++*off;
                    return true;
                }
                if ((l & 0xC0) == 0xC0) {
                    *off += 2;
                    return *off <= len;
                }
                if ((l & 0xC0) != 0) return false;  // RFC 6891 reserved
                *off += 1 + l;
            }
            return false;
        }

        // Encodes "a.b.c" into label wire format at out; returns the
        // byte count or 0 on malformed/oversized names.
        hcpp::size_t encode_name(const char* name, hcpp::u8* out,
                                 hcpp::size_t cap) noexcept {
            hcpp::size_t o = 0;
            while (*name != '\0') {
                const char* dot = name;
                hcpp::size_t label = 0;
                while (dot[label] != '\0' && dot[label] != '.') ++label;
                if (label == 0 || label > 63) return 0;
                if (o + 1 + label + 1 > cap) return 0;
                out[o++] = static_cast<hcpp::u8>(label);
                for (hcpp::size_t i = 0; i < label; ++i) out[o++] = name[i];
                name = dot + label;
                if (*name == '.') ++name;
            }
            if (o + 1 > cap) return 0;
            out[o++] = 0;
            return o;
        }

        void on_reply(hcpp::u32 src_ip, hcpp::u16 src_port,
                      const hcpp::u8* data, hcpp::size_t length) noexcept {
            (void)src_ip;
            (void)src_port;
            if (data == nullptr || length < sizeof(DnsHdr)) return;
            const DnsHdr* h = reinterpret_cast<const DnsHdr*>(data);
            if (dns_ntohs(h->id) != query_id) return;
            const hcpp::u16 flags = dns_ntohs(h->flags);
            if ((flags & 0x8000u) == 0) return;          // not a response
            if ((flags & 0x000Fu) != 0) return;          // rcode != NOERROR
            const hcpp::u16 qd = dns_ntohs(h->qdcount);
            const hcpp::u16 an = dns_ntohs(h->ancount);
            hcpp::size_t off = sizeof(DnsHdr);
            for (hcpp::u16 i = 0; i < qd; ++i) {
                if (!skip_name(data, length, &off)) return;
                if (off + 4 > length) return;
                off += 4;  // qtype + qclass
            }
            for (hcpp::u16 i = 0; i < an; ++i) {
                if (!skip_name(data, length, &off)) return;
                if (off + 10 > length) return;
                const hcpp::u16 type = static_cast<hcpp::u16>(
                    (data[off] << 8) | data[off + 1]);
                const hcpp::u16 rdlen = static_cast<hcpp::u16>(
                    (data[off + 8] << 8) | data[off + 9]);
                off += 10;  // type, class, ttl, rdlength
                if (off + rdlen > length) return;
                if (type == 1 && rdlen == 4) {  // A record
                    reply_addr = dns_be32(data + off);
                    reply_done = true;
                    return;
                }
                off += rdlen;
            }
        }

    }  // namespace

    bool dns_resolve(const char* name, hcpp::u32* ip_out,
                     hcpp::u32 timeout_ms) noexcept {
        bool ok = false;
        hcpp::u32 result = 0;
        if (net_ready() && name != nullptr && ip_out != nullptr) {
            // Server pick: DHCP option 6, else the gateway (QEMU
            // slirp answers DNS on both 10.0.2.2 and 10.0.2.3).
            hcpp::u32 server = net_dns_ip();
            if (server == 0) server = net_gateway();
            if (server == 0) server = ip_slirp_dns;
            if (server != 0) {
                hcpp::u8 query[dns_query_max];
                hcpp::set(query, 0, sizeof(query));
                DnsHdr* h = reinterpret_cast<DnsHdr*>(query);
                query_id = static_cast<hcpp::u16>(
                    (interrupts::pit_ticks() ^ 0x4849u) & 0xFFFFu);
                h->id = dns_htons(query_id);
                h->flags = dns_htons(0x0100);  // RD
                h->qdcount = dns_htons(1);
                const hcpp::size_t nlen =
                    encode_name(name, query + sizeof(DnsHdr),
                                sizeof(query) - sizeof(DnsHdr) - 4);
                if (nlen != 0) {
                    hcpp::size_t off = sizeof(DnsHdr) + nlen;
                    query[off++] = 0;
                    query[off++] = 1;  // QTYPE=A
                    query[off++] = 0;
                    query[off++] = 1;  // QCLASS=IN
                    reply_done = false;
                    reply_addr = 0;
                    (void)net_udp_bind(dns_port_local, on_reply);
                    (void)net_udp_send(server, dns_port_server,
                                       dns_port_local, query, off);
                    const hcpp::u64 limit =
                        timeout_ms / ms_per_tick == 0
                            ? 1
                            : timeout_ms / ms_per_tick;
                    const hcpp::u64 start = interrupts::pit_ticks();
                    while (interrupts::pit_ticks() - start < limit) {
                        net_poll();
                        if (reply_done) break;
                        interrupts::pit_wait_ticks(1);
                    }
                    if (reply_done) {
                        result = reply_addr;
                        *ip_out = reply_addr;
                        ok = true;
                    }
                }
            }
        }
        serial::write("dns ");
        serial::write(name != nullptr ? name : "?");
        serial::write("=");
        if (ok) {
            write_ip(result);
        } else {
            serial::write("fail");
        }
        serial::write("\r\n");
        return ok;
    }

    bool dns_selftest() noexcept {
        hcpp::u32 ip = 0;
        return dns_resolve("example.com", &ip, 3000);
    }

}  // namespace hype::net
