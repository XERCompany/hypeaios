#pragma once

// AMDTP stream lifecycle: owns an AmdtpCodec plus the stream state —
// isochronous channel, link speed, packets-per-buffer sizing, run flag
// and the packet callback the iso pipe invokes per delivered packet.
// amdtpstreampipe.h wires a stream onto an OHCI iso context.

#include "kernel/firewire/amdtpam824.h"

namespace hype::fw {

    enum class AmdtpDir : hcpp::u8 {
        Tx = 0,  // host transmits audio to the node (playback)
        Rx = 1,  // host receives audio from the node (capture)
    };

    // Called for each received iso packet payload (bytes after the iso
    // packet header quadlet), i.e. starting at the CIP header.
    using AmdtpPacketFn = void (*)(const hcpp::u8 *payload, hcpp::u32 bytes,
                                   void *ctx) noexcept;

    struct AmdtpStream {
        bool used;
        AmdtpDir dir;
        AmdtpCodec codec;
        hcpp::u8 channel;       // 0..63 once started
        hcpp::u8 speed;         // negotiated S-code
        hcpp::u8 ctx_index;     // OHCI iso context bound by the pipe
        hcpp::u8 node;          // peer node id (0x3F = any/broadcast)
        hcpp::u32 packets_per_buffer;   // iso packets per DMA buffer set
        hcpp::u32 packet_payload_bytes; // encode: bytes per packet
        bool running;
        AmdtpPacketFn packet_fn;
        void *packet_ctx;
        hcpp::u32 packets_seen;
        hcpp::u32 buffers_refilled;
    };

    constexpr hcpp::size_t amdtp_stream_max = 4;

    // Set up codec + sizing. `channels`/`midi` describe one data block.
    bool amdtp_stream_open(AmdtpStream &stream, AmdtpDir dir,
                           hcpp::u8 channels, hcpp::u8 midi_quadlets,
                           hcpp::u32 rate, hcpp::u8 sid) noexcept;

    // Bind the stream to iso `channel` + OHCI `ctx_index` and mark it
    // running. Does not touch hardware itself — amdtpstreampipe does.
    bool amdtp_stream_start(AmdtpStream &stream, hcpp::u8 channel,
                            hcpp::u8 ctx_index, hcpp::u8 node) noexcept;

    void amdtp_stream_stop(AmdtpStream &stream) noexcept;
    bool amdtp_stream_running(const AmdtpStream &stream) noexcept;
    hcpp::u32 amdtp_stream_packets(const AmdtpStream &stream) noexcept;

    // RX-side entry: feed one received payload through dbc tracking and
    // the registered packet callback.
    void amdtp_stream_deliver(AmdtpStream &stream, const hcpp::u8 *payload,
                              hcpp::u32 bytes) noexcept;

}  // namespace hype::fw
