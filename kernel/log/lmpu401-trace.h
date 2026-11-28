#pragma once

#include "hcpp/types.hpp"

// MPU-401 receive trace ring (issue #115): a running-status-aware MIDI
// stream parser fed by lmpu401_recv/lmpu401_poll. Each completed message
// is decoded into a short human-readable line ("ch1 noteon 60 v=100",
// "ch3 cc 7 v=90", ...) and appended to a fixed ring for later
// inspection or a serial dump.
namespace hype::midi {

    constexpr hcpp::size_t lmpu401_trace_events = 32;
    constexpr hcpp::size_t lmpu401_trace_line_max = 48;

    enum class Lmpu401EventKind : hcpp::u8 {
        NoteOff = 0,
        NoteOn = 1,
        PolyAftertouch = 2,
        ControlChange = 3,
        ProgramChange = 4,
        ChannelAftertouch = 5,
        PitchBend = 6,
        System = 7,
        Realtime = 8,
        Raw = 9,
    };

    // Feeds one received byte into the parser; appends a decoded event
    // when a message completes. Running status and interleaved realtime
    // bytes are handled per the MIDI 1.0 spec.
    void lmpu401_trace_feed(hcpp::u8 byte) noexcept;

    hcpp::u32 lmpu401_trace_count() noexcept;               // total decoded events
    hcpp::u32 lmpu401_trace_size() noexcept;                // entries currently held
    // Ring read: index 0 is the oldest retained event.
    const char* lmpu401_trace_read(hcpp::u32 index) noexcept;
    Lmpu401EventKind lmpu401_trace_kind(hcpp::u32 index) noexcept;

    void lmpu401_trace_dump() noexcept;   // writes the ring to the serial log
    void lmpu401_trace_reset() noexcept;

    // Feeds a synthetic stream (note on/off + control change) through the
    // parser and verifies the decoded ring. No hardware required.
    bool lmpu401_trace_selftest() noexcept;

}  // namespace hype::midi
