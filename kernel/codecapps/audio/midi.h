#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec {

    // MIDI SMF (MThd/MTrk): full event parse (VLQ deltas, running status,
    // channel events, tempo map, pitch bend) then synthesis to PCM through
    // the built-in wavetable synth — no SF2 bank; GM programs map to
    // sine/square/saw/triangle/noise voices (issue wave-2).
    struct MidiInfo {
        hcpp::u16 format;      // SMF format 0/1/2
        hcpp::u16 tracks;      // MTrk count
        hcpp::u16 division;    // ticks per quarter note (or SMPTE)
        hcpp::u16 channels;    // channels that saw a note-on
        hcpp::u32 notes;       // note-on events
        hcpp::u32 events;      // merged events
        hcpp::u32 duration_ms; // tempo-map-aware song length
        bool truncated;        // event cap hit
    };

    // Parses the whole file, merges tracks by tick and reports info.
    // Scratch comes from `arena` (released by the caller's arena_reset).
    bool midi_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                      MidiInfo &info) noexcept;

    // Renders the song to PcmBuf (S16 stereo at midi_render_rate). When
    // buf->data is nullptr the PCM is allocated from the arena; a
    // caller-supplied buffer renders min(song, capacity) frames.
    CodecStatus audio_midi_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept;

    constexpr hcpp::u32 midi_render_rate = 22050;
    constexpr hcpp::u32 midi_max_ms = 180000;

}  // namespace hype::codec
