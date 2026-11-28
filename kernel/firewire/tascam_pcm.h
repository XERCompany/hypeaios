#pragma once

// TASCAM playback path parameters: maps a PCM format (rate + channel
// count) onto AM824 stream parameters — the sampling-frequency code
// used in the CIP FDF field and the data-block size (dbs) in quadlets.
// Playback itself streams through an IT context via amdtpstreampipe;
// here we keep the format mapping + the open/close lifecycle hooks.

#include "hcpp/types.hpp"
#include "kernel/firewire/tascam_devkey.h"

namespace hype::fw {

    struct TascamPcmParams {
        hcpp::u32 rate;
        hcpp::u8 sfc;             // CIP fdf sampling-frequency code
        hcpp::u8 channels;        // PCM channels asked for
        hcpp::u8 dbs;             // data block size in quadlets
        hcpp::u8 midi_quadlets;   // midi quadlets inside dbs
        hcpp::u32 blocks_per_packet;
        hcpp::u32 packet_payload_bytes;  // 8 + dbs*4*blocks
    };

    // Fill `out` for a playback stream at `rate` on device `dev`.
    // Clamps channels to the device's playback width and always carries
    // the device's midi ports inside the data block. Returns false when
    // the rate has no sfc.
    bool tascam_pcm_params(const TascamDevKey &dev, hcpp::u32 rate,
                           hcpp::u8 channels, TascamPcmParams &out) noexcept;

    // Start/stop the playback stream on `node`. Skeleton on real link:
    // the IT context is programmed by the pipe; audio frames arrive from
    // the caller through tascam_playback_fill (24-bit interleaved).
    bool tascam_playback_start(const TascamDevKey *dev, hcpp::u8 node,
                               hcpp::u32 rate, hcpp::u8 channels) noexcept;
    void tascam_playback_stop() noexcept;
    bool tascam_playback_running() noexcept;

}  // namespace hype::fw
