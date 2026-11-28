#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/arena.h"

// Shared ProTracker-style tick engine (wave-2): MOD and XM front-ends
// both lower their file formats into a TkSong — an order list, a flat
// pattern cell grid and a sample table — then tk_render()/tk_simulate()
// sequence it: per-row cell effects, per-tick effect processing and a
// linear-interpolated stereo mixer.

namespace hype::codec {

    constexpr hcpp::u32 tk_max_channels = 32;
    constexpr hcpp::u32 tk_max_samples = 64;
    constexpr hcpp::u32 tk_max_patterns = 128;
    constexpr hcpp::u32 tk_max_orders = 256;

    struct TkCell {
        hcpp::u16 period;  // amiga-style period (0 = none)
        hcpp::u8 sample;   // 1-based sample index (0 = keep)
        hcpp::u8 fx;       // tracker effect nibble (0-F)
        hcpp::u8 param;
        hcpp::u8 vol;      // XM volume column (0 = none); 0 for MOD cells
        hcpp::u8 note;     // XM note value for key-off (97); 0 for MOD
    };

    struct TkSample {
        const hcpp::i8 *data;
        hcpp::u32 length;
        hcpp::u32 loop_off;
        hcpp::u32 loop_len;
        hcpp::u8 volume;    // 0-64
        hcpp::i8 finetune;  // MOD: -8..7 (1/8 semitone); XM: -128..127 → scaled
        hcpp::i8 relnote;   // XM relative note (semitones); 0 for MOD
        hcpp::u8 pad2;
    };

    struct TkSong {
        hcpp::u8 channels;
        hcpp::u8 num_samples;
        hcpp::u8 num_patterns;
        hcpp::u8 pad0;
        hcpp::u16 order_count;
        hcpp::u8 restart;      // order to restart at (0xFF = stop)
        hcpp::u8 pad1;
        hcpp::u8 order[tk_max_orders];
        hcpp::u16 pat_rows[tk_max_patterns];   // rows per pattern
        hcpp::u32 pat_off[tk_max_patterns];    // cell index of pattern start
        TkCell *cells;                          // arena-owned flat grid
        TkSample samples[tk_max_samples];
        hcpp::u8 tempo;                        // ticks per row
        hcpp::u16 bpm;
        hcpp::u32 duration_ms;                 // filled by tk_simulate
        hcpp::u32 rows_played;
    };

    // Allocates a zeroed cell grid of `total_cells` entries from `arena`.
    bool tk_cells_alloc(TkSong &song, hcpp::u32 total_cells, Arena &arena) noexcept;

    // Sequences the song without mixing; returns frames the render will
    // produce and fills duration_ms/rows_played. Safe to call on a
    // freshly-parsed TkSong; engine state is local.
    hcpp::u32 tk_simulate(TkSong &song, hcpp::u32 rate) noexcept;

    // Renders the song into `dst` (S16 stereo interleaved, `max_frames`
    // frames of capacity). Returns frames written.
    hcpp::u32 tk_render(const TkSong &song, hcpp::i16 *dst, hcpp::u32 max_frames,
                        hcpp::u32 rate) noexcept;

}  // namespace hype::codec
