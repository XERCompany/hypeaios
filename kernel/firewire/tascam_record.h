#pragma once

// TASCAM record path: AMDTP capture stream decoded into an s16 stereo
// PCM ring that firewireALSA exposes as a pullmix source. The device
// streams up to `capture_channels` MBLA inputs; the record path folds
// channel pairs down to stereo so anything the desk sends lands in the
// mixer without extra plumbing.

#include "hcpp/types.hpp"
#include "kernel/firewire/tascam_devkey.h"

namespace hype::fw {

    constexpr hcpp::u32 tascam_ring_frames = 8192;  // stereo frames

    struct TascamCapture {
        bool running;
        const TascamDevKey *dev;
        hcpp::u8 node;
        hcpp::u32 rate;
        hcpp::u8 channels;      // device capture channels
        hcpp::i16 ring[tascam_ring_frames * 2];
        hcpp::u32 head;         // consumer position (frames)
        hcpp::u32 tail;         // producer position (frames)
        hcpp::u32 underruns;
        hcpp::u32 overflow_frames;
        hcpp::u64 frames_captured;
    };

    // Start the capture ring (decode target). The iso pipe fills it via
    // tascam_capture_block() calls from the bus's RX packet consumer.
    bool tascam_capture_start(const TascamDevKey *dev, hcpp::u8 node,
                              hcpp::u32 rate) noexcept;
    void tascam_capture_stop() noexcept;
    bool tascam_capture_running() noexcept;

    // Producer: push `frames` data blocks of interleaved 24-bit samples
    // (`channels` per frame) into the stereo ring — folds odd/extra
    // channels into L/R by pairs.
    void tascam_capture_block(const hcpp::i32 *pcm24, hcpp::u32 frames,
                              hcpp::u8 channels) noexcept;

    // Consumer (pullmix fill shape): read up to `frames` stereo s16
    // frames; underrun emits silence.
    hcpp::u32 tascam_capture_read(hcpp::i16 *dst, hcpp::u32 frames) noexcept;

    TascamCapture *tascam_capture() noexcept;

}  // namespace hype::fw
