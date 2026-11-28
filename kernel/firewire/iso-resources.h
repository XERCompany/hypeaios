#pragma once

// Isochronous resource manager (IRM) helpers — IEEE 1394 channel and
// bandwidth allocation done as compare-swap lock transactions against
// the bus's IRM CSRs (csr_channels_available_* and
// csr_bandwidth_available at CSR_BASE + 0x220/0x224). The IRM is the
// node whose self-ID reports it as IRM-capable (irmc in the bus-info
// block) with the highest node id; when no IRM answered (single-node
// bus, absent link) the functions fall back to claiming resources
// locally so the stream paths still exercise their plumbing.

#include "hcpp/types.hpp"

namespace hype::fw {

    // Returns the negotiated IRM node id, or 0xFF when no IRM was
    // identified (the allocator then runs in local-only mode).
    hcpp::u8 fw_irm_node() noexcept;

    // Allocate the first free channel (0..63) via compare-swap on
    // CHANNELS_AVAILABLE. Returns the channel number or -1.
    int fw_irm_alloc_channel() noexcept;

    // Release a previously allocated channel.
    bool fw_irm_free_channel(hcpp::u32 channel) noexcept;

    // Rough bandwidth-allocation-units estimate for an iso packet of
    // `payload_bytes` sent every 125 us at `speed` (S-code). IEEE 1394
    // units are ~20 ns quanta of the ~125 us cycle; a full S1600 frame
    // budget is 6144 units.
    hcpp::u32 fw_irm_bandwidth_units(hcpp::u8 speed,
                                   hcpp::u32 payload_bytes) noexcept;

    // Reserve/release `units` via compare-swap on BANDWIDTH_AVAILABLE.
    bool fw_irm_alloc_bandwidth(hcpp::u32 units) noexcept;
    bool fw_irm_free_bandwidth(hcpp::u32 units) noexcept;

    // Snapshot of the local channel-allocation bitmask (bit n set =
    // channel n in use). Useful for selftests and diagnostics.
    hcpp::u64 fw_irm_channels_in_use() noexcept;

}  // namespace hype::fw
