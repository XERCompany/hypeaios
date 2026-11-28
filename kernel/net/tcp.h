#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal TCP (RFC 793) on top of the IPv4 stack. Documented
    // limits, by design:
    //  - 16 static TCBs; no dynamic allocation.
    //  - Stop-and-wait data transfer: a single outstanding payload
    //    segment per connection (tcp_send blocks, bounded, until the
    //    ACK or the retransmit budget expires).
    //  - No out-of-order reassembly: only segments arriving exactly at
    //    rcv_nxt are buffered; anything else is dropped after a
    //    duplicate ACK and recovered via peer retransmission.
    //  - 8 KiB receive buffer per connection == advertised window.
    //  - Fixed 1 s RTO, <=3 retransmits of SYN/data/FIN; TIME_WAIT uses
    //    a shortened 2 s (not 2*MSL) before the TCB is freed.
    //  - Simultaneous-open FIN transitions are simplified (FIN_WAIT_1 +
    //    FIN -> TIME_WAIT rather than CLOSING).
    //
    // All waits are bounded: blocking entry points spin on net_poll()
    // plus PIT ticks and always return on timeout.

    enum class TcpState : hcpp::u8 {
        Closed = 0,
        Listen,
        SynSent,
        SynRcvd,
        Established,
        FinWait1,
        FinWait2,
        CloseWait,
        LastAck,
        TimeWait,
    };

    // Opaque TCB handle; invalid after tcp_close() or once tcp_state()
    // reports Closed (the slot may be reused by a later connection).
    struct TcpConn;

    // Active open: sends SYN (MSS option) with up to 3 retransmits and
    // waits up to timeout_ms for ESTABLISHED while driving net_poll().
    // nullptr on timeout, RST, or a full TCB table.
    TcpConn* tcp_connect(hcpp::u32 ip, hcpp::u16 port,
                         hcpp::u32 timeout_ms) noexcept;
    // Passive open: parks a TCB in LISTEN on the local port; the caller
    // polls tcp_state() for Established after an inbound SYN handshake.
    // nullptr when the table is full or the port is already bound.
    TcpConn* tcp_listen(hcpp::u16 port) noexcept;
    // Stop-and-wait send: splits data into <= min(MSS, peer window)
    // chunks, each chunk outstanding until ACKed (bounded wait).
    // Returns the number of bytes accepted by the peer's ACK stream.
    hcpp::size_t tcp_send(TcpConn* c, const void* data,
                          hcpp::size_t length) noexcept;
    // Drains up to cap bytes of buffered in-order data; waits up to
    // timeout_ms for the first byte. Returns 0 on timeout or when the
    // peer's FIN has been consumed and the buffer is empty. Buffered
    // bytes are still returned after the connection dies.
    hcpp::size_t tcp_recv(TcpConn* c, void* buf, hcpp::size_t cap,
                          hcpp::u32 timeout_ms) noexcept;
    // Graceful close: FIN (+short bounded wait for the FIN-ACK) from
    // ESTABLISHED/CLOSE_WAIT; immediate teardown otherwise. Frees the
    // TCB once the handshake or the retransmit budget completes.
    void tcp_close(TcpConn* c) noexcept;
    TcpState tcp_state(const TcpConn* c) noexcept;
    // True once the peer's FIN has been consumed (recv-side EOF).
    bool tcp_peer_closed(const TcpConn* c) noexcept;
    // True after RST or retransmit-exhaustion abort.
    bool tcp_failed(const TcpConn* c) noexcept;

    // QEMU slirp selftest: resolves example.com, connects to port 80,
    // sends a minimal HTTP/1.0 GET, and expects response bytes starting
    // with "HTTP". Prints "tcp connect=ok|fail|skip", "tcp tx=N",
    // "tcp rx=N", and "tcp selftest=ok|fail|skip". Fully bounded.
    bool tcp_selftest() noexcept;

    // ---- stack plumbing (called from kernel/net/net.cc) ----

    // IPv4 protocol-6 dispatch: segment is the TCP header + payload,
    // src/dst IPv4 addresses in host byte order.
    void tcp_input(hcpp::u32 src_ip, hcpp::u32 dst_ip,
                   const hcpp::u8* segment, hcpp::size_t length) noexcept;
    // Retransmit + TIME_WAIT housekeeping; driven from net_poll().
    void tcp_timer_poll() noexcept;

}  // namespace hype::net
