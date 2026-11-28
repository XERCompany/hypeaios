#pragma once

#include "kernel/codecapps/codec.h"

namespace hype::codec {

    // ProTracker MOD (31 instruments, M.K./xCHN/xxCH magic at 1080):
    // pattern table + packed cells {period, sample, effect, param} played
    // through a tick-based effect engine (arpeggio, porta, vibrato,
    // volume slides, position/pattern flow, E-commands) and a linear-
    // interpolated stereo mixer (issue wave-2).
    struct ModInfo {
        hcpp::u8 channels;
        hcpp::u8 song_length;    // order count
        hcpp::u8 patterns;       // distinct patterns referenced
        hcpp::u8 instruments;    // sample slots with data
        hcpp::u32 rows;          // rows sequenced
        hcpp::u32 duration_ms;
        bool looped;             // song end / position jump bound hit
    };

    // Parses the module (headers + pattern data bounds) and simulates the
    // song length; no audio is rendered.
    bool mod_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                     ModInfo &info) noexcept;

    // Renders the whole song to PcmBuf (S16 stereo at mod_render_rate).
    CodecStatus audio_mod_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                 void *output, Arena &arena) noexcept;

    // XM (Extended Module) header/pattern/instrument parser. XM playback
    // reuses the MOD tick engine after mapping notes to periods; XM-only
    // features (envelopes, multi-sample instruments, most volume-column
    // commands) are noted as unsupported fields of XmInfo.
    struct XmInfo {
        hcpp::u16 channels;
        hcpp::u16 patterns;
        hcpp::u16 instruments;
        hcpp::u16 song_length;
        hcpp::u16 rows_total;    // sum of per-pattern row counts
        hcpp::u8 flags;          // bit0: linear frequency table
        bool has_samples;
    };

    bool xm_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                    XmInfo &info) noexcept;
    CodecStatus audio_xm_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                void *output, Arena &arena) noexcept;

    constexpr hcpp::u32 mod_render_rate = 22050;
    constexpr hcpp::u32 mod_max_ms = 240000;

}  // namespace hype::codec
