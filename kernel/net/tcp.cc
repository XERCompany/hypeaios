#include "kernel/net/tcp.h"

#include "hcpp/memory.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/net/dns.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        constexpr hcpp::u8 tcp_fin = 0x01;
        constexpr hcpp::u8 tcp_syn = 0x02;
        constexpr hcpp::u8 tcp_rst = 0x04;
        constexpr hcpp::u8 tcp_psh = 0x08;
        constexpr hcpp::u8 tcp_ack = 0x10;

        constexpr hcpp::size_t tcp_conn_max = 16;
        constexpr hcpp::size_t tcp_rx_buf_size = 8192;  // advertised window
        constexpr hcpp::size_t tcp_tx_buf_size = 1500;  // rtx copy of 1 seg
        constexpr hcpp::u16 tcp_mss_local = 1460;       // eth MTU - hdrs
        constexpr hcpp::u16 tcp_mss_default = 536;      // until SYN option
        constexpr hcpp::u16 tcp_port_ephemeral = 49152;

        // PIT runs at 100 Hz, so one tick is 10 ms.
        constexpr hcpp::u32 ms_per_tick = 10;
        constexpr hcpp::u64 tcp_rto_ticks = 100;        // 1 s fixed RTO
        constexpr hcpp::u8 tcp_rtx_max = 3;
        constexpr hcpp::u64 tcp_time_wait_ticks = 200;  // shortened 2*MSL
        constexpr hcpp::u64 tcp_wnd_wait_ticks = 300;   // 3 s zero-window
        constexpr hcpp::u64 tcp_close_wait_ticks = 50;  // FIN-ACK grace

        constexpr hcpp::u8 fl_reset = 1u << 0;  // peer sent RST
        constexpr hcpp::u8 fl_fin = 1u << 1;    // peer FIN consumed
        constexpr hcpp::u8 fl_abort = 1u << 2;  // retransmit exhaustion

        constexpr hcpp::u8 rtx_none = 0;
        constexpr hcpp::u8 rtx_syn = 1;   // SYN or SYN+ACK, seq == iss
        constexpr hcpp::u8 rtx_data = 2;  // payload in tx_buf
        constexpr hcpp::u8 rtx_fin = 3;   // FIN, seq == snd_nxt - 1

    }  // namespace

    // TCB: the type opaque to callers (forward-declared in tcp.h).
    // tx_buf keeps a copy of the single outstanding payload segment for
    // retransmission; rx_buf is the in-order receive window.
    struct TcpConn {
        TcpState state;
        hcpp::u8 flags;
        hcpp::u8 rtx_kind;
        hcpp::u8 rtx_count;
        hcpp::u16 local_port;
        hcpp::u16 remote_port;
        hcpp::u16 mss;          // peer's advertised MSS
        hcpp::u16 tx_len;       // outstanding payload in tx_buf
        hcpp::u32 remote_ip;    // host byte order
        hcpp::u32 iss;
        hcpp::u32 snd_una;
        hcpp::u32 snd_nxt;
        hcpp::u16 snd_wnd;      // peer's advertised window
        hcpp::u16 rx_len;       // buffered in-order bytes
        hcpp::u32 irs;
        hcpp::u32 rcv_nxt;
        hcpp::u64 rtx_deadline;
        hcpp::u64 state_deadline;  // TIME_WAIT expiry
        hcpp::u8 tx_buf[1500];   // tcp_tx_buf_size
        hcpp::u8 rx_buf[8192];   // tcp_rx_buf_size
    };

    namespace {

        struct TcpHdr {
            hcpp::u16 src_port;
            hcpp::u16 dst_port;
            hcpp::u32 seq;
            hcpp::u32 ack;
            hcpp::u8 off_res;  // data offset in the high nibble
            hcpp::u8 flags;
            hcpp::u16 window;
            hcpp::u16 csum;
            hcpp::u16 urgent;
        } __attribute__((packed));

        TcpConn conns[tcp_conn_max];
        hcpp::u16 next_ephemeral = tcp_port_ephemeral;
        // Reentrancy guard: tcp_input()/tcp_timer_poll() can transmit,
        // and a transmit can nest net_poll() (inline ARP resolve), which
        // would re-enter TCP. Nested input is dropped instead (the peer
        // retransmits); nested timer ticks are skipped.
        bool tcp_busy;
        hcpp::u32 stat_rx_segs;
        hcpp::u32 stat_csum_drop;
        hcpp::u8 seg_buf[sizeof(TcpHdr) + 8 + tcp_tx_buf_size];

        constexpr hcpp::u16 n16(hcpp::u16 v) noexcept {
            return static_cast<hcpp::u16>((v >> 8) | (v << 8));
        }
        constexpr hcpp::u32 n32(hcpp::u32 v) noexcept {
            return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
                   (v << 24);
        }
        void be16(hcpp::u8* p, hcpp::u16 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v >> 8);
            p[1] = static_cast<hcpp::u8>(v);
        }
        void be32(hcpp::u8* p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v >> 24);
            p[1] = static_cast<hcpp::u8>(v >> 16);
            p[2] = static_cast<hcpp::u8>(v >> 8);
            p[3] = static_cast<hcpp::u8>(v);
        }

        // Sequence-space comparisons, correct across 2^32 wraparound.
        bool seq_lt(hcpp::u32 a, hcpp::u32 b) noexcept {
            return static_cast<int>(a - b) < 0;
        }
        bool seq_gt(hcpp::u32 a, hcpp::u32 b) noexcept {
            return static_cast<int>(a - b) > 0;
        }

        hcpp::u32 sum16(hcpp::u32 sum, const hcpp::u8* p,
                        hcpp::size_t n) noexcept {
            while (n > 1) {
                sum += static_cast<hcpp::u16>((p[0] << 8) | p[1]);
                p += 2;
                n -= 2;
            }
            if (n != 0) sum += static_cast<hcpp::u16>(p[0] << 8);
            return sum;
        }

        hcpp::u16 sum_fold(hcpp::u32 sum) noexcept {
            while ((sum >> 16) != 0) sum = (sum & 0xFFFFu) + (sum >> 16);
            return static_cast<hcpp::u16>(~sum & 0xFFFFu);
        }

        // Pseudo-header + segment checksum; with the segment's csum
        // field included the result is 0 for a valid packet.
        hcpp::u16 tcp_csum(hcpp::u32 src_ip, hcpp::u32 dst_ip,
                           const hcpp::u8* seg, hcpp::size_t len) noexcept {
            hcpp::u8 ph[12];
            be32(ph, src_ip);
            be32(ph + 4, dst_ip);
            ph[8] = 0;
            ph[9] = 6;  // IPPROTO_TCP
            be16(ph + 10, static_cast<hcpp::u16>(len));
            return sum_fold(sum16(sum16(0, ph, sizeof(ph)), seg, len));
        }

        hcpp::u16 rx_window(const TcpConn& c) noexcept {
            const hcpp::u32 free_bytes = tcp_rx_buf_size - c.rx_len;
            return static_cast<hcpp::u16>(
                free_bytes > 0xFFFFu ? 0xFFFFu : free_bytes);
        }

        // Builds one segment into seg_buf and transmits it. seq/ack are
        // host byte order; data may be nullptr for control segments.
        // Not reentrant (shared seg_buf); callers are serialized by
        // tcp_busy or run on the application side only.
        bool xmit(TcpConn& c, hcpp::u32 seq, hcpp::u32 ack, hcpp::u8 flags,
                  const hcpp::u8* data, hcpp::size_t len,
                  bool mss_opt) noexcept {
            if (len > tcp_tx_buf_size) return false;
            TcpHdr* h = reinterpret_cast<TcpHdr*>(seg_buf);
            h->src_port = n16(c.local_port);
            h->dst_port = n16(c.remote_port);
            h->seq = n32(seq);
            h->ack = n32(ack);
            h->flags = flags;
            h->window = n16(rx_window(c));
            h->csum = 0;
            h->urgent = 0;
            hcpp::size_t hdr = sizeof(TcpHdr);
            if (mss_opt) {
                hcpp::u8* o = seg_buf + hdr;
                o[0] = 2;  // MSS option
                o[1] = 4;
                be16(o + 2, tcp_mss_local);
                hdr += 4;
            }
            h->off_res = static_cast<hcpp::u8>((hdr / 4) << 4);
            if (data != nullptr && len != 0)
                hcpp::copy(seg_buf + hdr, data, len);
            const hcpp::size_t total = hdr + len;
            h->csum = n16(tcp_csum(net_ip(), c.remote_ip, seg_buf, total));
            return net_tcp_send_segment(c.remote_ip, seg_buf, total);
        }

        // RST for a segment that matched no TCB. Per RFC 793: if the
        // incoming segment carried ACK, reflect seq=seg.ack (bare RST);
        // otherwise RST|ACK with ack = seq + consumed sequence space.
        void xmit_reset(hcpp::u32 dst_ip, hcpp::u16 our_port,
                        hcpp::u16 their_port, hcpp::u32 seq, hcpp::u32 ack,
                        hcpp::u8 flags) noexcept {
            TcpConn fake{};
            fake.local_port = our_port;
            fake.remote_port = their_port;
            fake.remote_ip = dst_ip;
            (void)xmit(fake, seq, ack, flags, nullptr, 0, false);
        }

        void send_ack(TcpConn& c) noexcept {
            (void)xmit(c, c.snd_nxt, c.rcv_nxt, tcp_ack, nullptr, 0, false);
        }

        void arm_rtx(TcpConn& c, hcpp::u8 kind) noexcept {
            c.rtx_kind = kind;
            c.rtx_count = 0;
            c.rtx_deadline = interrupts::pit_ticks() + tcp_rto_ticks;
        }

        void mark_closed(TcpConn& c, hcpp::u8 extra_flags) noexcept {
            c.state = TcpState::Closed;
            c.flags |= extra_flags;
            c.rtx_kind = rtx_none;
            c.tx_len = 0;
        }

        // Walks TCP options for kind 2 (MSS); falls back to the
        // RFC 793 default of 536 when absent or malformed.
        hcpp::u16 parse_mss(const hcpp::u8* opt, hcpp::size_t len) noexcept {
            hcpp::size_t i = 0;
            while (i < len) {
                const hcpp::u8 kind = opt[i];
                if (kind == 0) break;          // end of option list
                if (kind == 1) {               // NOP
                    ++i;
                    continue;
                }
                if (i + 1 >= len) break;
                const hcpp::u8 olen = opt[i + 1];
                if (olen < 2 || i + olen > len) break;
                if (kind == 2 && olen == 4) {
                    const hcpp::u16 mss = static_cast<hcpp::u16>(
                        (opt[i + 2] << 8) | opt[i + 3]);
                    return mss == 0 ? tcp_mss_default : mss;
                }
                i += olen;
            }
            return tcp_mss_default;
        }

        hcpp::u32 gen_iss(hcpp::size_t slot) noexcept {
            return static_cast<hcpp::u32>(interrupts::pit_ticks()) *
                       2654435761u +
                   static_cast<hcpp::u32>(slot) * 7919u;
        }

        bool local_port_used(hcpp::u16 port) noexcept {
            for (hcpp::size_t i = 0; i < tcp_conn_max; ++i)
                if (conns[i].state != TcpState::Closed &&
                    conns[i].local_port == port)
                    return true;
            return false;
        }

        TcpConn* alloc_conn() noexcept {
            for (hcpp::size_t i = 0; i < tcp_conn_max; ++i) {
                if (conns[i].state == TcpState::Closed) {
                    TcpConn& c = conns[i];
                    hcpp::set(&c, 0, sizeof(TcpConn) -
                                       sizeof(c.tx_buf) - sizeof(c.rx_buf));
                    c.state = TcpState::Closed;  // fields above were reset
                    return &c;
                }
            }
            return nullptr;
        }

        TcpConn* find_conn(hcpp::u16 dst_port, hcpp::u32 src_ip,
                           hcpp::u16 src_port) noexcept {
            for (hcpp::size_t i = 0; i < tcp_conn_max; ++i) {
                TcpConn& c = conns[i];
                if (c.state == TcpState::Closed ||
                    c.state == TcpState::Listen)
                    continue;
                if (c.local_port == dst_port && c.remote_ip == src_ip &&
                    c.remote_port == src_port)
                    return &c;
            }
            return nullptr;
        }

        TcpConn* find_listener(hcpp::u16 dst_port) noexcept {
            for (hcpp::size_t i = 0; i < tcp_conn_max; ++i)
                if (conns[i].state == TcpState::Listen &&
                    conns[i].local_port == dst_port)
                    return &conns[i];
            return nullptr;
        }

        // Shared segment body for the synchronized states: ACK
        // processing (incl. FIN acknowledgement), in-order data into
        // rx_buf, FIN consumption. Out-of-order data is dropped after
        // a duplicate ACK (documented no-reassembly limit).
        void process_body(TcpConn& c, hcpp::u32 seg_seq, hcpp::u32 seg_ack,
                          hcpp::u8 flags, hcpp::u16 win,
                          const hcpp::u8* data, hcpp::size_t data_len,
                          hcpp::u32 fin_seq, bool& acked_fin) noexcept {
            if ((flags & tcp_ack) != 0) {
                if (seq_gt(seg_ack, c.snd_nxt)) {
                    // ACK for something we never sent: RFC says send an
                    // ACK back and drop the segment.
                    send_ack(c);
                    return;
                }
                if (seq_gt(seg_ack, c.snd_una)) {
                    c.snd_una = seg_ack;
                    c.snd_wnd = win;
                    if (c.rtx_kind == rtx_data &&
                        !seq_lt(seg_ack, c.snd_nxt)) {
                        c.tx_len = 0;
                        c.rtx_kind = rtx_none;
                    }
                    if (c.rtx_kind == rtx_fin && seg_ack == c.snd_nxt) {
                        // Our FIN consumed exactly one sequence number.
                        c.rtx_kind = rtx_none;
                        acked_fin = true;
                        if (c.state == TcpState::FinWait1)
                            c.state = TcpState::FinWait2;
                        else if (c.state == TcpState::LastAck)
                            mark_closed(c, 0);
                    }
                } else {
                    // Duplicate/old ACK: still refresh the send window.
                    c.snd_wnd = win;
                }
            }

            const bool may_recv =
                c.state == TcpState::Established ||
                c.state == TcpState::FinWait1 ||
                c.state == TcpState::FinWait2;
            if (data_len != 0 && may_recv) {
                // Trim a retransmitted leading edge we already acked.
                if (seq_lt(seg_seq, c.rcv_nxt)) {
                    const hcpp::u32 skip = c.rcv_nxt - seg_seq;
                    if (skip >= data_len) {
                        data_len = 0;
                    } else {
                        data += skip;
                        data_len -= skip;
                        seg_seq = c.rcv_nxt;
                    }
                }
                if (data_len != 0) {
                    if (seg_seq == c.rcv_nxt) {
                        const hcpp::size_t room =
                            tcp_rx_buf_size - c.rx_len;
                        const hcpp::size_t take =
                            data_len < room ? data_len : room;
                        if (take != 0) {
                            hcpp::copy(c.rx_buf + c.rx_len, data, take);
                            c.rx_len += static_cast<hcpp::u16>(take);
                            c.rcv_nxt += static_cast<hcpp::u32>(take);
                        }
                        send_ack(c);
                        // take < data_len means the window filled: the
                        // short ACK makes the peer retransmit the rest.
                    } else {
                        // Out-of-order: drop and duplicate-ACK.
                        send_ack(c);
                    }
                }
            }

            // FIN counts as one sequence number *after* the payload; it
            // is consumed only once rcv_nxt reaches it (in-order rule).
            if ((flags & tcp_fin) != 0 && fin_seq == c.rcv_nxt &&
                !((c.flags & fl_fin) != 0)) {
                c.rcv_nxt += 1;
                c.flags |= fl_fin;
                send_ack(c);
                switch (c.state) {
                case TcpState::Established:
                    c.state = TcpState::CloseWait;
                    break;
                case TcpState::FinWait1:
                case TcpState::FinWait2:
                    // Simplified: no CLOSING state; our FIN may remain
                    // unacked but TIME_WAIT still answers re-FINs.
                    c.rtx_kind = rtx_none;
                    c.state = TcpState::TimeWait;
                    c.state_deadline =
                        interrupts::pit_ticks() + tcp_time_wait_ticks;
                    break;
                case TcpState::CloseWait:
                case TcpState::LastAck:
                case TcpState::TimeWait:
                    break;  // peer re-FIN: ACK already sent
                default:
                    break;
                }
            } else if ((flags & tcp_fin) != 0 &&
                       (c.flags & fl_fin) != 0) {
                send_ack(c);  // retransmitted FIN
            }
        }

    }  // namespace

    void tcp_input(hcpp::u32 src_ip, hcpp::u32 dst_ip,
                   const hcpp::u8* segment, hcpp::size_t length) noexcept {
        if (tcp_busy || length < sizeof(TcpHdr)) return;
        tcp_busy = true;

        const TcpHdr* h = reinterpret_cast<const TcpHdr*>(segment);
        const hcpp::u16 src_port = n16(h->src_port);
        const hcpp::u16 dst_port = n16(h->dst_port);
        const hcpp::u32 seg_seq = n32(h->seq);
        const hcpp::u32 seg_ack = n32(h->ack);
        const hcpp::size_t hdr_len =
            static_cast<hcpp::size_t>(h->off_res >> 4) * 4;
        const hcpp::u8 flags = h->flags;
        const hcpp::u16 win = n16(h->window);
        bool done = false;

        if (hdr_len < sizeof(TcpHdr) || hdr_len > length) {
            done = true;
        } else if (tcp_csum(src_ip, dst_ip, segment, length) != 0) {
            ++stat_csum_drop;
            done = true;
        }

        const hcpp::u8* data = segment + hdr_len;
        hcpp::size_t data_len = length - hdr_len;
        // Sequence number at which a FIN in this segment sits.
        const hcpp::u32 fin_seq =
            seg_seq + static_cast<hcpp::u32>(data_len);

        TcpConn* c = nullptr;
        if (!done) {
            c = find_conn(dst_port, src_ip, src_port);
            if (c == nullptr) c = find_listener(dst_port);
            if (c == nullptr) {
                // No TCB: RST unless the segment was itself a RST.
                if ((flags & tcp_rst) == 0) {
                    const hcpp::u32 consumed =
                        static_cast<hcpp::u32>(data_len) +
                        ((flags & tcp_syn) != 0 ? 1u : 0u) +
                        ((flags & tcp_fin) != 0 ? 1u : 0u);
                    if ((flags & tcp_ack) != 0) {
                        xmit_reset(src_ip, dst_port, src_port, seg_ack, 0,
                                   tcp_rst);
                    } else {
                        xmit_reset(src_ip, dst_port, src_port, 0,
                                   seg_seq + consumed,
                                   static_cast<hcpp::u8>(tcp_rst | tcp_ack));
                    }
                }
                done = true;
            } else {
                ++stat_rx_segs;
            }
        }

        if (!done) {
            if ((flags & tcp_rst) != 0) {
                mark_closed(*c, fl_reset);
            } else if (c->state == TcpState::Listen) {
                if ((flags & tcp_syn) != 0) {
                    c->remote_ip = src_ip;
                    c->remote_port = src_port;
                    c->irs = seg_seq;
                    c->rcv_nxt = seg_seq + 1;
                    c->iss = gen_iss(c - conns);
                    c->snd_una = c->iss;
                    c->snd_nxt = c->iss + 1;
                    c->snd_wnd = win;
                    c->mss = parse_mss(segment + sizeof(TcpHdr),
                                       hdr_len - sizeof(TcpHdr));
                    (void)xmit(*c, c->iss, c->rcv_nxt,
                               static_cast<hcpp::u8>(tcp_syn | tcp_ack),
                               nullptr, 0, true);
                    arm_rtx(*c, rtx_syn);
                    c->state = TcpState::SynRcvd;
                } else if ((flags & tcp_ack) != 0) {
                    xmit_reset(src_ip, dst_port, src_port, seg_ack, 0,
                               tcp_rst);
                } else {
                    xmit_reset(src_ip, dst_port, src_port, 0,
                               seg_seq + static_cast<hcpp::u32>(data_len),
                               static_cast<hcpp::u8>(tcp_rst | tcp_ack));
                }
            } else if (c->state == TcpState::SynSent) {
                if ((flags & (tcp_syn | tcp_ack)) == (tcp_syn | tcp_ack)) {
                    if (seg_ack == c->snd_nxt) {
                        c->irs = seg_seq;
                        c->rcv_nxt = seg_seq + 1;
                        c->snd_una = seg_ack;
                        c->snd_wnd = win;
                        c->mss = parse_mss(segment + sizeof(TcpHdr),
                                           hdr_len - sizeof(TcpHdr));
                        c->rtx_kind = rtx_none;
                        send_ack(*c);
                        c->state = TcpState::Established;
                    } else {
                        xmit_reset(src_ip, dst_port, src_port, seg_ack, 0,
                                   tcp_rst);
                        mark_closed(*c, fl_reset);
                    }
                } else if ((flags & tcp_syn) != 0) {
                    // Simultaneous open: SYN without ACK -> SYN_RCVD.
                    c->irs = seg_seq;
                    c->rcv_nxt = seg_seq + 1;
                    c->mss = parse_mss(segment + sizeof(TcpHdr),
                                       hdr_len - sizeof(TcpHdr));
                    (void)xmit(*c, c->iss, c->rcv_nxt,
                               static_cast<hcpp::u8>(tcp_syn | tcp_ack),
                               nullptr, 0, true);
                    c->state = TcpState::SynRcvd;
                }
            } else if (c->state == TcpState::SynRcvd) {
                if ((flags & tcp_ack) != 0 && seg_ack == c->snd_nxt) {
                    c->snd_una = seg_ack;
                    c->snd_wnd = win;
                    c->rtx_kind = rtx_none;
                    c->state = TcpState::Established;
                }
            } else {
                bool acked_fin = false;
                process_body(*c, seg_seq, seg_ack, flags, win, data,
                             data_len, fin_seq, acked_fin);
                (void)acked_fin;
            }
        }

        tcp_busy = false;
    }

    void tcp_timer_poll() noexcept {
        if (tcp_busy) return;
        tcp_busy = true;
        const hcpp::u64 now = interrupts::pit_ticks();
        for (hcpp::size_t i = 0; i < tcp_conn_max; ++i) {
            TcpConn& c = conns[i];
            if (c.state == TcpState::Closed) continue;
            if (c.state == TcpState::TimeWait &&
                static_cast<hcpp::i64>(now - c.state_deadline) >= 0) {
                mark_closed(c, 0);
                continue;
            }
            if (c.rtx_kind == rtx_none ||
                static_cast<hcpp::i64>(now - c.rtx_deadline) < 0)
                continue;
            if (c.rtx_count >= tcp_rtx_max) {
                mark_closed(c, fl_abort);
                continue;
            }
            ++c.rtx_count;
            c.rtx_deadline =
                now + (tcp_rto_ticks << (c.rtx_count - 1));  // backoff
            switch (c.rtx_kind) {
            case rtx_syn:
                if (c.state == TcpState::SynSent) {
                    (void)xmit(c, c.iss, 0, tcp_syn, nullptr, 0, true);
                } else if (c.state == TcpState::SynRcvd) {
                    (void)xmit(c, c.iss, c.rcv_nxt,
                               static_cast<hcpp::u8>(tcp_syn | tcp_ack),
                               nullptr, 0, true);
                } else {
                    c.rtx_kind = rtx_none;
                }
                break;
            case rtx_data:
                if (c.tx_len != 0) {
                    (void)xmit(c, c.snd_nxt - c.tx_len, c.rcv_nxt,
                               static_cast<hcpp::u8>(tcp_ack | tcp_psh),
                               c.tx_buf, c.tx_len, false);
                } else {
                    c.rtx_kind = rtx_none;
                }
                break;
            case rtx_fin:
                (void)xmit(c, c.snd_nxt - 1, c.rcv_nxt,
                           static_cast<hcpp::u8>(tcp_fin | tcp_ack),
                           nullptr, 0, false);
                break;
            default:
                c.rtx_kind = rtx_none;
                break;
            }
        }
        tcp_busy = false;
    }

    TcpConn* tcp_connect(hcpp::u32 ip, hcpp::u16 port,
                         hcpp::u32 timeout_ms) noexcept {
        if (!net_ready() || ip == 0 || port == 0) return nullptr;
        TcpConn* c = alloc_conn();
        if (c == nullptr) return nullptr;
        // Pick a free ephemeral port; the scan is bounded by the table.
        for (hcpp::u32 tries = 0; tries < tcp_conn_max + 4; ++tries) {
            if (!local_port_used(next_ephemeral)) break;
            ++next_ephemeral;
            if (next_ephemeral < tcp_port_ephemeral)
                next_ephemeral = tcp_port_ephemeral;
        }
        c->local_port = next_ephemeral++;
        if (next_ephemeral < tcp_port_ephemeral ||
            local_port_used(c->local_port)) {
            // Wrapped or raced: fall back to the first free port.
            hcpp::u16 p = tcp_port_ephemeral;
            while (p != 0 && local_port_used(p)) ++p;
            if (p == 0) return nullptr;
            c->local_port = p;
        }
        c->remote_ip = ip;
        c->remote_port = port;
        c->iss = gen_iss(c - conns);
        c->snd_una = c->iss;
        c->snd_nxt = c->iss;
        c->snd_wnd = tcp_rx_buf_size;
        c->mss = tcp_mss_default;
        c->state = TcpState::SynSent;
        if (!xmit(*c, c->iss, 0, tcp_syn, nullptr, 0, true)) {
            mark_closed(*c, fl_abort);
            return nullptr;
        }
        c->snd_nxt = c->iss + 1;
        arm_rtx(*c, rtx_syn);

        hcpp::u64 limit = timeout_ms / ms_per_tick;
        if (limit == 0) limit = 1;
        const hcpp::u64 deadline = interrupts::pit_ticks() + limit;
        while (c->state == TcpState::SynSent ||
               c->state == TcpState::SynRcvd) {
            net_poll();
            if (c->state == TcpState::Established) break;
            if (c->state == TcpState::Closed) break;
            if (static_cast<hcpp::i64>(interrupts::pit_ticks() - deadline) >=
                0) {
                mark_closed(*c, fl_abort);
                break;
            }
            interrupts::pit_wait_ticks(1);
        }
        return c->state == TcpState::Established ? c : nullptr;
    }

    TcpConn* tcp_listen(hcpp::u16 port) noexcept {
        if (!net_ready() || port == 0) return nullptr;
        if (find_listener(port) != nullptr) return nullptr;
        TcpConn* c = alloc_conn();
        if (c == nullptr) return nullptr;
        c->local_port = port;
        c->mss = tcp_mss_default;
        c->state = TcpState::Listen;
        return c;
    }

    hcpp::size_t tcp_send(TcpConn* c, const void* data,
                          hcpp::size_t length) noexcept {
        if (c == nullptr || data == nullptr || length == 0) return 0;
        if (c->state != TcpState::Established &&
            c->state != TcpState::CloseWait)
            return 0;
        const hcpp::u8* p = static_cast<const hcpp::u8*>(data);
        hcpp::size_t sent = 0;
        while (sent < length) {
            if (c->state != TcpState::Established &&
                c->state != TcpState::CloseWait)
                break;
            // Bounded zero-window wait.
            const hcpp::u64 wstart = interrupts::pit_ticks();
            while (c->snd_wnd == 0 &&
                   (c->flags & (fl_reset | fl_abort)) == 0 &&
                   interrupts::pit_ticks() - wstart < tcp_wnd_wait_ticks) {
                net_poll();
                interrupts::pit_wait_ticks(1);
            }
            if (c->snd_wnd == 0) break;
            hcpp::size_t chunk = length - sent;
            if (chunk > c->mss) chunk = c->mss;
            if (chunk > c->snd_wnd) chunk = c->snd_wnd;
            if (chunk > tcp_tx_buf_size) chunk = tcp_tx_buf_size;
            if (chunk == 0) break;
            hcpp::copy(c->tx_buf, p + sent, chunk);
            c->tx_len = static_cast<hcpp::u16>(chunk);
            if (!xmit(*c, c->snd_nxt, c->rcv_nxt,
                      static_cast<hcpp::u8>(tcp_ack | tcp_psh), c->tx_buf,
                      chunk, false))
                break;
            c->snd_nxt += static_cast<hcpp::u32>(chunk);
            arm_rtx(*c, rtx_data);
            // Stop-and-wait: block (bounded) until the ACK clears
            // rtx_kind, or the retransmit budget runs out.
            const hcpp::u64 deadline =
                interrupts::pit_ticks() +
                tcp_rto_ticks * (tcp_rtx_max + 1) + 50;
            while (c->rtx_kind == rtx_data &&
                   (c->flags & (fl_reset | fl_abort)) == 0 &&
                   static_cast<hcpp::i64>(interrupts::pit_ticks() -
                                          deadline) < 0) {
                net_poll();
                interrupts::pit_wait_ticks(1);
            }
            if (c->rtx_kind == rtx_data ||
                (c->flags & (fl_reset | fl_abort)) != 0)
                break;
            sent += chunk;
        }
        return sent;
    }

    hcpp::size_t tcp_recv(TcpConn* c, void* buf, hcpp::size_t cap,
                          hcpp::u32 timeout_ms) noexcept {
        if (c == nullptr || buf == nullptr || cap == 0) return 0;
        hcpp::u64 limit = timeout_ms / ms_per_tick;
        if (limit == 0) limit = 1;
        const hcpp::u64 deadline = interrupts::pit_ticks() + limit;
        while (c->rx_len == 0) {
            if ((c->flags & (fl_fin | fl_reset | fl_abort)) != 0 ||
                c->state == TcpState::Closed ||
                c->state == TcpState::Listen)
                return 0;
            if (static_cast<hcpp::i64>(interrupts::pit_ticks() - deadline) >=
                0)
                return 0;
            net_poll();
            interrupts::pit_wait_ticks(1);
        }
        const hcpp::size_t n = cap < c->rx_len ? cap : c->rx_len;
        hcpp::copy(buf, c->rx_buf, n);
        // Compact: linear buffer keeps only in-order bytes.
        c->rx_len -= static_cast<hcpp::u16>(n);
        if (c->rx_len != 0)
            hcpp::copy(c->rx_buf, c->rx_buf + n, c->rx_len);
        return n;
    }

    void tcp_close(TcpConn* c) noexcept {
        if (c == nullptr) return;
        switch (c->state) {
        case TcpState::Closed:
        case TcpState::Listen:
            mark_closed(*c, 0);
            return;
        case TcpState::SynSent:
        case TcpState::SynRcvd:
            // Half-formed handshake: RST and drop the TCB.
            (void)xmit(*c, c->snd_nxt, 0, tcp_rst, nullptr, 0, false);
            mark_closed(*c, 0);
            return;
        case TcpState::Established:
            c->state = TcpState::FinWait1;
            break;
        case TcpState::CloseWait:
            c->state = TcpState::LastAck;
            break;
        default:
            return;  // FIN already in flight
        }
        (void)xmit(*c, c->snd_nxt, c->rcv_nxt,
                   static_cast<hcpp::u8>(tcp_fin | tcp_ack), nullptr, 0,
                   false);
        c->snd_nxt += 1;
        arm_rtx(*c, rtx_fin);
        // Short bounded wait for the FIN-ACK; the connection keeps
        // draining through net_poll() afterwards regardless.
        const hcpp::u64 deadline =
            interrupts::pit_ticks() + tcp_close_wait_ticks;
        while (c->rtx_kind == rtx_fin &&
               (c->flags & (fl_reset | fl_abort)) == 0 &&
               static_cast<hcpp::i64>(interrupts::pit_ticks() - deadline) <
                   0) {
            net_poll();
            interrupts::pit_wait_ticks(1);
        }
    }

    TcpState tcp_state(const TcpConn* c) noexcept {
        return c != nullptr ? c->state : TcpState::Closed;
    }

    bool tcp_peer_closed(const TcpConn* c) noexcept {
        return c != nullptr && (c->flags & fl_fin) != 0;
    }

    bool tcp_failed(const TcpConn* c) noexcept {
        return c != nullptr && (c->flags & (fl_reset | fl_abort)) != 0;
    }

    bool tcp_selftest() noexcept {
        if (!net_ready()) {
            serial::write_line("tcp selftest=skip");
            return false;
        }
        hcpp::u32 ip = 0;
        if (!dns_resolve("example.com", &ip, 3000) || ip == 0) {
            serial::write_line("tcp connect=skip");
            serial::write_line("tcp selftest=skip");
            return false;
        }
        TcpConn* c = tcp_connect(ip, 80, 8000);
        serial::write("tcp connect=");
        serial::write_line(c != nullptr ? "ok" : "fail");
        if (c == nullptr) {
            serial::write_line("tcp selftest=fail");
            return false;
        }
        static const char req[] =
            "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
        const hcpp::size_t sent = tcp_send(c, req, sizeof(req) - 1);
        serial::write("tcp tx=");
        serial::write_dec32(static_cast<hcpp::u32>(sent));
        serial::write("\r\n");

        hcpp::u8 buf[512];
        hcpp::u32 total = 0;
        bool http_seen = false;
        const hcpp::u64 deadline = interrupts::pit_ticks() + 1200;  // 12 s
        while (static_cast<hcpp::i64>(interrupts::pit_ticks() - deadline) <
               0) {
            const hcpp::size_t n = tcp_recv(c, buf, sizeof(buf), 500);
            for (hcpp::size_t i = 0; i + 3 < n; ++i) {
                if (buf[i] == 'H' && buf[i + 1] == 'T' && buf[i + 2] == 'T' &&
                    buf[i + 3] == 'P')
                    http_seen = true;
            }
            total += static_cast<hcpp::u32>(n);
            if (n == 0 &&
                (tcp_peer_closed(c) || tcp_failed(c) ||
                 tcp_state(c) == TcpState::Closed))
                break;
        }
        serial::write("tcp rx=");
        serial::write_dec32(total);
        serial::write(" segs=");
        serial::write_dec32(stat_rx_segs);
        serial::write(" csumdrop=");
        serial::write_dec32(stat_csum_drop);
        serial::write(" http=");
        serial::write_line(http_seen ? "ok" : "none");
        tcp_close(c);
        // Give the FIN handshake a moment on the wire.
        const hcpp::u64 drain_end = interrupts::pit_ticks() + 30;
        while (static_cast<hcpp::i64>(interrupts::pit_ticks() - drain_end) <
               0) {
            net_poll();
            interrupts::pit_wait_ticks(1);
        }
        const bool ok = total != 0 && http_seen && sent == sizeof(req) - 1;
        serial::write("tcp selftest=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

}  // namespace hype::net
