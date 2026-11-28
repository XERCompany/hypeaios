#pragma once

// Digi 002/003 streaming session state — owns the AMDTP stream pair
// (host→device playback + device→host capture), the negotiated iso
// channels, current rate/clock/optical-mode shadow and the begin/end
// session protocol against the device's CSR control space.
//
// Session lifecycle (mirroring Linux digi00x-stream.c):
//   begin:  write ISOC_CHANNELS = tx<<16|rx, write STREAMING_SET=1,
//           poll STREAMING_STATE until 1.
//   finish: write STREAMING_SET=3, poll STREAMING_STATE until 0.
//
// Reference: Linux sound/firewire/digi00x/digi00x-stream.c.

#include "hcpp/types.hpp"
#include "kernel/chips/digi00x/digi00x.h"
#include "kernel/firewire/amdtpstream.h"

namespace hype::chips::digi00x {

    // Quadlet read/write transaction callback — bound to the
    // FireWire async layer at runtime so the stream layer stays
    // transport-agnostic.
    using Dg00xReadFn = bool (*)(void *ctx, hcpp::u64 addr,
                                 hcpp::u32 *out) noexcept;
    using Dg00xWriteFn = bool (*)(void *ctx, hcpp::u64 addr,
                                  hcpp::u32 value) noexcept;

    struct Dg00xStream {
        const Dg00xModel *model;
        Dg00xRate rate;
        Dg00xClock clock;
        Dg00xOptMode opt;
        hcpp::u8 tx_channel;   // iso ch the node transmits on (Rx side)
        hcpp::u8 rx_channel;   // iso ch the node receives on (Tx side)
        hype::fw::AmdtpStream tx;   // host → node playback
        hype::fw::AmdtpStream rx;   // node → host capture
        bool streaming;
        // transport bindings
        void *bus_ctx;
        Dg00xReadFn read;
        Dg00xWriteFn write;
    };

    // Register address helper: 64-bit CSR address for a register.
    inline hcpp::u64 dg00x_addr(hcpp::u16 reg) noexcept {
        return dg00x_addr_base | reg;
    }

    // Write one control register through the bound transport.
    inline bool dg00x_reg_write(Dg00xStream &s, hcpp::u16 reg,
                                hcpp::u32 value) noexcept {
        if (s.write == nullptr) return false;
        return s.write(s.bus_ctx, dg00x_addr(reg), value);
    }
    inline bool dg00x_reg_read(Dg00xStream &s, hcpp::u16 reg,
                               hcpp::u32 &out) noexcept {
        if (s.read == nullptr) return false;
        return s.read(s.bus_ctx, dg00x_addr(reg), &out);
    }

    // Number of AMDTP data-block channels at the current rate
    // (PCM + one MIDI channel on the first data channel).
    inline hcpp::u8 dg00x_data_block_channels(
        const Dg00xStream &s) noexcept {
        return static_cast<hcpp::u8>(pcm_channels(s.rate));
    }

    // Session-begin word assembly.
    inline hcpp::u32 dg00x_begin_isoc_word(const Dg00xStream &s) noexcept {
        return isoc_channels_word(s.tx_channel, s.rx_channel);
    }

    // Poll bound for STREAMING_STATE transitions (quadlet reads).
    constexpr hcpp::u32 stream_poll_max = 3000;

}  // namespace hype::chips::digi00x
