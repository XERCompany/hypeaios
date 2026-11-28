#pragma once

// ALSA-facing shim: bridges a running AMDTP RX stream into the kernel
// audio layer. Decoded stereo s16 frames sit in the tascam capture
// ring; this shim registers a pullmix source with the same fill-
// callback shape as the Mp3FileSource used for the decoded mp3 test
// (kernel/main.cc + kernel/audio/pullmix.h pullmix_add_source).

#include "hcpp/types.hpp"

namespace hype::fwalsa {

    // Idempotent: once the TASCAM capture stream is running and the
    // mixer output is up (hasio ready), register the capture ring as a
    // stereo pullmix source at the stream's sample rate. Called from the
    // kernel idle loop via fwalsa_pump(); safe to call any time.
    bool fwalsa_register_source() noexcept;

    // True after a pullmix source id was allocated for the fw stream.
    bool fwalsa_streaming() noexcept;
    hcpp::u32 fwalsa_source_id() noexcept;

    // Idle-loop entry: registers the source lazily and keeps stats fresh.
    void fwalsa_pump() noexcept;

}  // namespace hype::fwalsa
