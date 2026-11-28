#pragma once

// Iso pipe: binds an AmdtpStream to a hardware iso channel through an
// OHCI iso DMA context. RX pipes program an IR context (packet-per-
// buffer + isochHeader) whose buffers the bus layer scans in fw_poll();
// TX pipes program an IT context that would stream encoded packets on
// every 125 us cycle (skeleton: descriptors + context registers are
// programmed, cycle scheduling is not exercised).

#include "kernel/firewire/amdtpstream.h"
#include "kernel/firewire/firewirebus.h"

namespace hype::fw {

    struct FwIsoPipe {
        bool used;
        bool rx;               // true: IR context, false: IT context
        hcpp::u8 channel;
        hcpp::u8 ctx_index;    // OHCI context, fw_no_iso_ctx until armed
        AmdtpStream *stream;
        hcpp::u32 packets_delivered;
    };

    constexpr hcpp::size_t fw_iso_pipe_max = 4;

    // Open a pipe for `stream` on `channel`: allocates + programs the
    // OHCI iso context. RX also registers the stream's deliver path as
    // the bus-level packet consumer. nullptr on failure.
    FwIsoPipe *fw_iso_pipe_open(AmdtpStream &stream, hcpp::u8 channel,
                                hcpp::u8 node) noexcept;

    // Stop the context and release the pipe slot + iso channel.
    void fw_iso_pipe_close(FwIsoPipe *pipe) noexcept;

    hcpp::u32 fw_iso_pipe_count() noexcept;

}  // namespace hype::fw
