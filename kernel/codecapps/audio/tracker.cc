#include "kernel/codecapps/audio/tracker.h"

// ProTracker-style sequencing engine (wave-2). Timing: `speed` ticks per
// row, `bpm` sets tick duration = rate * 2.5 / bpm output frames.
// Periods are amiga-style (freq = PAL_CLOCK / (2*period)); finetune is
// applied as a 2^(ft/96) multiplier when stepping. Implemented effect
// subset: 0 arpeggio, 1/2 porta, 3 tone-porta, 4 vibrato, 5/6 combo,
// 7 tremolo, 9 sample offset, A volume slide, B/D flow, C set volume,
// E1/E2 fine porta, E6 pattern loop, E9 retrig, EA/EB fine volume,
// EC note cut, ED note delay, EE pattern delay, F speed/BPM.

extern "C" double sin(double x) noexcept;
extern "C" double pow(double x, double y) noexcept;

namespace hype::codec {

    namespace {

        constexpr double pal_clock = 7093789.2;
        constexpr hcpp::u32 max_seq_rows = 8192;

        hcpp::i16 vib_lut[64];
        bool vib_lut_ready = false;

        void vib_lut_build() noexcept {
            for (hcpp::size_t i = 0; i < 64; ++i)
                vib_lut[i] = static_cast<hcpp::i16>(
                    64.0 * sin(6.283185307179586 * i / 64.0));
            vib_lut_ready = true;
        }

        struct TkChan {
            hcpp::u8 smp;
            hcpp::u8 vol;
            hcpp::u8 vol_trem;
            hcpp::u8 pan;      // 0..128
            hcpp::u16 base;    // period after porta slides
            hcpp::u16 period;  // effective period incl. vibrato/arpeggio
            hcpp::u16 target;  // tone-porta destination
            hcpp::u8 fx;
            hcpp::u8 param;
            hcpp::u8 porta_speed;
            hcpp::u8 vib_phase, vib_speed, vib_depth;
            hcpp::u8 trem_phase, trem_speed, trem_depth;
            hcpp::u8 off_hi;   // 9xx offset memory
            hcpp::u8 loop_row, loop_left;
            hcpp::u8 gliss;
            double ft_mult;    // finetune multiplier of current sample
            const hcpp::i8 *data;
            hcpp::u32 len, loop_off, loop_len;
            hcpp::u32 pos, step;  // 16.16 fixed point
            bool on;
            TkCell pend;
            bool has_pend;
        };

        struct Eng {
            const TkSong *s;
            TkChan ch[tk_max_channels];
            hcpp::u16 order;
            hcpp::u16 row;
            hcpp::u8 tick;
            hcpp::u8 speed;
            hcpp::u16 bpm;
            hcpp::u32 rate;
            hcpp::u32 tick_left;
            hcpp::u32 row_ticks;
            hcpp::u32 rows;
            bool done;
            bool looped;
            hcpp::u8 jump_order;  // 0xFF = none
            hcpp::u8 jump_row;
            hcpp::u8 pat_delay;
            hcpp::u32 frames;
        };

        hcpp::u32 tick_frames(const Eng &e) noexcept {
            if (e.bpm < 32) return e.rate / 40;
            return static_cast<hcpp::u32>(static_cast<double>(e.rate) * 2.5 / e.bpm);
        }

        void chan_step(TkChan &ch, hcpp::u32 rate) noexcept {
            if (ch.period == 0) {
                ch.step = 0;
                return;
            }
            const double freq = pal_clock / (2.0 * ch.period) * ch.ft_mult;
            ch.step = static_cast<hcpp::u32>(freq * 65536.0 / rate);
        }

        void chan_point_sample(Eng &e, TkChan &ch) noexcept {
            if (ch.smp >= e.s->num_samples) return;
            const TkSample &s = e.s->samples[ch.smp];
            ch.data = s.data;
            ch.len = s.length;
            ch.loop_off = s.loop_off;
            ch.loop_len = s.loop_len;
            ch.ft_mult = pow(2.0, static_cast<double>(s.finetune) / 96.0);
        }

        void chan_trigger(Eng &e, TkChan &ch, hcpp::u16 period) noexcept {
            if (ch.smp >= e.s->num_samples) return;
            const TkSample &s = e.s->samples[ch.smp];
            chan_point_sample(e, ch);
            if (ch.fx == 9) {
                if (ch.param != 0) ch.off_hi = ch.param;
                ch.pos = static_cast<hcpp::u32>(ch.off_hi) << 16;
            } else {
                ch.pos = 0;
            }
            ch.base = ch.period = period;
            ch.on = s.data != nullptr && s.length != 0;
            if (s.loop_len > 2 && s.loop_off + s.loop_len <= s.length)
                ch.loop_len = s.loop_len;
            else
                ch.loop_len = 0;
            chan_step(ch, e.rate);
            ch.vol_trem = ch.vol;
        }

        // XM note -> pseudo-MOD period: XM note 49 (C-4, relnote 0) plays
        // at 8363 Hz; we invert the amiga period relation so the shared
        // step math lands on the XM frequency. Honest approximation —
        // linear/amiga XM flags are treated the same.
        hcpp::u16 xm_note_period(Eng &e, const TkChan &ch, hcpp::u8 note) noexcept {
            if (ch.smp >= e.s->num_samples) return 0;
            const TkSample &s = e.s->samples[ch.smp];
            const double semis = static_cast<double>(note) - 49.0 +
                                 static_cast<double>(s.relnote);
            const double freq = 8363.0 * pow(2.0, semis / 12.0);
            if (freq <= 0.0) return 0;
            const double period = pal_clock / (2.0 * freq);
            if (period < 1.0) return 1;
            if (period > 32767.0) return 32767;
            return static_cast<hcpp::u16>(period);
        }

        // Applies the note/instrument part of a cell (tick-0 semantics, or
        // the delayed application under E D note-delay).
        void chan_cell(Eng &e, TkChan &ch, const TkCell &cell) noexcept {
            ch.fx = cell.fx;
            ch.param = cell.param;
            if (cell.sample != 0) {
                const hcpp::u8 idx = cell.sample - 1;
                if (idx < e.s->num_samples) {
                    ch.smp = idx;
                    ch.vol = e.s->samples[idx].volume;
                    ch.vol_trem = ch.vol;
                    if (cell.period == 0 && cell.note == 0)
                        chan_point_sample(e, ch);
                }
            }
            if (cell.note == 97) {  // XM key-off
                ch.on = false;
            }
            hcpp::u16 period = cell.period;
            if (period == 0 && cell.note != 0 && cell.note != 97)
                period = xm_note_period(e, ch, cell.note);
            if (period != 0) {
                if (cell.fx == 3 || cell.fx == 5) {
                    ch.target = period;  // tone porta: slide, no trigger
                } else {
                    chan_trigger(e, ch, period);
                }
            }
        }

        hcpp::u8 bcd(hcpp::u8 v) noexcept {
            return static_cast<hcpp::u8>((v >> 4) * 10 + (v & 0x0F));
        }

        void process_row(Eng &e) noexcept {
            e.row_ticks = e.speed;
            e.pat_delay = 0;
            if (e.order >= e.s->order_count) {
                e.done = true;
                return;
            }
            const hcpp::u8 pat = e.s->order[e.order];
            if (pat >= e.s->num_patterns) {
                e.done = true;
                return;
            }
            const TkCell *row = e.s->cells + e.s->pat_off[pat] +
                                static_cast<hcpp::u32>(e.row) * e.s->channels;
            ++e.rows;
            for (hcpp::u32 c = 0; c < e.s->channels; ++c) {
                TkChan &ch = e.ch[c];
                const TkCell cell = row[c];
                const bool delayed = cell.fx == 0xE && (cell.param >> 4) == 0xD;
                if (delayed) {
                    ch.pend = cell;
                    ch.has_pend = true;
                    ch.fx = cell.fx;
                    ch.param = cell.param;
                } else {
                    chan_cell(e, ch, cell);
                }
                // XM volume column: 0x10-0x50 = set volume.
                if (cell.vol >= 0x10 && cell.vol <= 0x50)
                    ch.vol = ch.vol_trem = cell.vol - 0x10;
                // Tick-0 effects.
                const hcpp::u8 fx = cell.fx;
                const hcpp::u8 p = cell.param;
                switch (fx) {
                    case 3:
                        ch.porta_speed = p;
                        break;
                    case 4:
                        if ((p >> 4) != 0) ch.vib_speed = p >> 4;
                        if ((p & 0x0F) != 0) ch.vib_depth = p & 0x0F;
                        break;
                    case 7:
                        if ((p >> 4) != 0) ch.trem_speed = p >> 4;
                        if ((p & 0x0F) != 0) ch.trem_depth = p & 0x0F;
                        break;
                    case 0xC:
                        ch.vol = ch.vol_trem = p > 64 ? 64 : p;
                        break;
                    case 0xB:
                        e.jump_order = p;
                        e.jump_row = 0;
                        break;
                    case 0xD:
                        e.jump_order = static_cast<hcpp::u8>(e.order + 1);
                        e.jump_row = bcd(p);
                        break;
                    case 0xE:
                        switch (p >> 4) {
                            case 1:
                                ch.base = ch.period =
                                    ch.base > (p & 0x0F) ? ch.base - (p & 0x0F) : 1;
                                chan_step(ch, e.rate);
                                break;
                            case 2:
                                ch.base = ch.period =
                                    static_cast<hcpp::u16>(ch.base + (p & 0x0F));
                                chan_step(ch, e.rate);
                                break;
                            case 3:
                                ch.gliss = p & 0x0F;
                                break;
                            case 6:
                                if ((p & 0x0F) == 0) {
                                    ch.loop_row = static_cast<hcpp::u8>(e.row);
                                } else if (ch.loop_left == 0) {
                                    ch.loop_left = p & 0x0F;
                                    e.jump_order = static_cast<hcpp::u8>(e.order);
                                    e.jump_row = ch.loop_row;
                                } else if (--ch.loop_left != 0) {
                                    e.jump_order = static_cast<hcpp::u8>(e.order);
                                    e.jump_row = ch.loop_row;
                                }
                                break;
                            case 0xA:
                                ch.vol = ch.vol + (p & 0x0F) > 64
                                             ? 64
                                             : ch.vol + (p & 0x0F);
                                ch.vol_trem = ch.vol;
                                break;
                            case 0xB:
                                ch.vol = ch.vol > (p & 0x0F) ? ch.vol - (p & 0x0F) : 0;
                                ch.vol_trem = ch.vol;
                                break;
                            case 0xE:
                                e.pat_delay = p & 0x0F;
                                break;
                            default:
                                break;  // E5 finetune, E7 tremwave, E8 pan,
                                        // EF funk — unsupported
                        }
                        break;
                    case 0xF:
                        if (p != 0) {
                            if (p < 0x20)
                                e.speed = p;
                            else
                                e.bpm = p;
                        }
                        break;
                    default:
                        break;  // 0/1/2/5/6/9/A per-tick; 8 pan ignored
                }
            }
            e.row_ticks = static_cast<hcpp::u32>(e.speed) * (1 + e.pat_delay);
        }

        void process_tick(Eng &e) noexcept {
            for (hcpp::u32 c = 0; c < e.s->channels; ++c) {
                TkChan &ch = e.ch[c];
                ch.vol_trem = ch.vol;
                const hcpp::u8 p = ch.param;
                switch (ch.fx) {
                    case 0:
                        if (p != 0) {
                            const hcpp::u8 which = static_cast<hcpp::u8>(e.tick % 3);
                            const hcpp::u8 semi = which == 1    ? p >> 4
                                                  : which == 2  ? p & 0x0F
                                                                : 0;
                            ch.period = static_cast<hcpp::u16>(
                                ch.base * pow(2.0, -static_cast<double>(semi) / 12.0));
                            chan_step(ch, e.rate);
                        }
                        break;
                    case 1:
                        ch.base = ch.base > p ? ch.base - p : 1;
                        ch.period = ch.base;
                        chan_step(ch, e.rate);
                        break;
                    case 2:
                        ch.base = ch.period =
                            static_cast<hcpp::u16>(ch.base + p);
                        chan_step(ch, e.rate);
                        break;
                    case 3:
                        if (ch.base < ch.target) {
                            ch.base = ch.base + ch.porta_speed < ch.target
                                          ? ch.base + ch.porta_speed
                                          : ch.target;
                        } else if (ch.base > ch.target) {
                            ch.base = ch.base > ch.target + ch.porta_speed
                                          ? ch.base - ch.porta_speed
                                          : ch.target;
                        }
                        if (ch.gliss != 0) {
                            // glissando: snap to semitone boundaries.
                            ch.period = static_cast<hcpp::u16>(
                                ch.base & ~static_cast<hcpp::u16>(0x0F));
                        } else {
                            ch.period = ch.base;
                        }
                        chan_step(ch, e.rate);
                        break;
                    case 4:
                        ch.period = static_cast<hcpp::u16>(
                            static_cast<hcpp::i32>(ch.base) +
                            (vib_lut[ch.vib_phase & 63] * ch.vib_depth >> 4));
                        ch.vib_phase += ch.vib_speed;
                        chan_step(ch, e.rate);
                        break;
                    case 5:
                    case 6: {
                        if (ch.fx == 5) {
                            if (ch.base < ch.target)
                                ch.base = ch.base + ch.porta_speed < ch.target
                                              ? ch.base + ch.porta_speed
                                              : ch.target;
                            else if (ch.base > ch.target)
                                ch.base = ch.base > ch.target + ch.porta_speed
                                              ? ch.base - ch.porta_speed
                                              : ch.target;
                            ch.period = ch.base;
                        } else {
                            ch.period = static_cast<hcpp::u16>(
                                static_cast<hcpp::i32>(ch.base) +
                                (vib_lut[ch.vib_phase & 63] * ch.vib_depth >> 4));
                            ch.vib_phase += ch.vib_speed;
                        }
                        chan_step(ch, e.rate);
                        const hcpp::i32 dv = (p >> 4) - (p & 0x0F);
                        const hcpp::i32 v = ch.vol + dv;
                        ch.vol = v < 0 ? 0 : v > 64 ? 64 : static_cast<hcpp::u8>(v);
                        break;
                    }
                    case 7: {
                        hcpp::i32 v = static_cast<hcpp::i32>(ch.vol) +
                                      (vib_lut[ch.trem_phase & 63] * ch.trem_depth >> 6);
                        if (v < 0) v = 0;
                        if (v > 64) v = 64;
                        ch.vol_trem = static_cast<hcpp::u8>(v);
                        ch.trem_phase += ch.trem_speed;
                        break;
                    }
                    case 0xA: {
                        const hcpp::i32 dv = (p >> 4) - (p & 0x0F);
                        const hcpp::i32 v = ch.vol + dv;
                        ch.vol = v < 0 ? 0 : v > 64 ? 64 : static_cast<hcpp::u8>(v);
                        ch.vol_trem = ch.vol;
                        break;
                    }
                    case 0xE:
                        switch (p >> 4) {
                            case 9:
                                if ((p & 0x0F) != 0 && e.tick % (p & 0x0F) == 0)
                                    ch.pos = 0;
                                break;
                            case 0xC:
                                if (e.tick == (p & 0x0F)) {
                                    ch.vol = 0;
                                    ch.vol_trem = 0;
                                }
                                break;
                            case 0xD:
                                if (ch.has_pend && e.tick == (p & 0x0F)) {
                                    const TkCell kept = ch.pend;
                                    chan_cell(e, ch, kept);
                                    ch.has_pend = false;
                                }
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        void advance(Eng &e) noexcept {
            if (e.done) return;
            if (e.tick + 1 < e.row_ticks) {
                ++e.tick;
                process_tick(e);
            } else {
                e.tick = 0;
                if (e.jump_order != 0xFF) {
                    if (e.jump_order < e.order ||
                        (e.jump_order == e.order && e.jump_row <= e.row))
                        e.looped = true;
                    e.order = e.jump_order;
                    e.row = e.jump_row;
                    e.jump_order = 0xFF;
                } else {
                    ++e.row;
                }
                while (e.order < e.s->order_count &&
                       (e.s->order[e.order] >= e.s->num_patterns ||
                        e.row >= e.s->pat_rows[e.s->order[e.order]])) {
                    ++e.order;
                    e.row = 0;
                }
                if (e.order >= e.s->order_count) {
                    e.done = true;
                    return;
                }
                process_row(e);
            }
            e.tick_left = tick_frames(e);
            if (e.tick_left == 0) e.tick_left = 1;
            if (e.rows >= max_seq_rows) e.done = true;
        }

        void eng_init(Eng &e, const TkSong &song, hcpp::u32 rate) noexcept {
            e = Eng{};
            e.s = &song;
            e.rate = rate;
            e.order = 0;
            e.row = 0;
            e.tick = 0;
            e.speed = song.tempo != 0 ? song.tempo : 6;
            e.bpm = song.bpm != 0 ? song.bpm : 125;
            e.jump_order = 0xFF;
            for (hcpp::u32 c = 0; c < tk_max_channels; ++c) {
                e.ch[c] = TkChan{};
                e.ch[c].smp = 0xFF;
                e.ch[c].ft_mult = 1.0;
                e.ch[c].pan = (c & 3) == 0 || (c & 3) == 3 ? 32 : 96;  // L R R L
            }
            process_row(e);
            e.tick_left = tick_frames(e);
        }

        // One output frame through the mixer.
        void mix_frame(Eng &e, hcpp::i16 *dst) noexcept {
            hcpp::i32 l = 0, r = 0;
            for (hcpp::u32 c = 0; c < e.s->channels; ++c) {
                TkChan &ch = e.ch[c];
                if (!ch.on || ch.data == nullptr || ch.len == 0) continue;
                hcpp::u32 idx = ch.pos >> 16;
                if (idx >= ch.len) {
                    if (ch.loop_len > 2 && ch.loop_off < ch.len) {
                        idx = ch.loop_off + (idx - ch.loop_off) % ch.loop_len;
                        ch.pos = (idx << 16) | (ch.pos & 0xFFFF);
                    } else {
                        ch.on = false;
                        continue;
                    }
                }
                hcpp::u32 next = idx + 1;
                if (next >= ch.len)
                    next = ch.loop_len > 2 && ch.loop_off < ch.len ? ch.loop_off : idx;
                const hcpp::i32 a = ch.data[idx];
                const hcpp::i32 b = ch.data[next];
                const hcpp::i32 frac = static_cast<hcpp::i32>((ch.pos >> 8) & 0xFF);
                const hcpp::i32 s = (a + ((b - a) * frac >> 8)) * ch.vol_trem;
                l += s * (128 - ch.pan);
                r += s * ch.pan;
                ch.pos += ch.step;
            }
            l >>= 7;
            r >>= 7;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            if (dst != nullptr) {
                dst[0] = static_cast<hcpp::i16>(l);
                dst[1] = static_cast<hcpp::i16>(r);
            }
        }

        hcpp::u32 tk_run(const TkSong &song, hcpp::i16 *dst, hcpp::u32 max_frames,
                         hcpp::u32 rate, bool mix) noexcept {
            if (!vib_lut_ready) vib_lut_build();
            Eng e;
            eng_init(e, song, rate);
            while (!e.done && e.frames < max_frames) {
                if (e.tick_left == 0) advance(e);
                if (e.done) break;
                if (mix) {
                    mix_frame(e, dst != nullptr ? dst + e.frames * 2 : nullptr);
                    ++e.frames;
                    --e.tick_left;
                } else {
                    e.frames += e.tick_left;
                    e.tick_left = 0;
                }
            }
            return e.frames;
        }

    }  // namespace

    bool tk_cells_alloc(TkSong &song, hcpp::u32 total_cells, Arena &arena) noexcept {
        song.cells = static_cast<TkCell *>(
            arena_alloc(arena, static_cast<hcpp::size_t>(total_cells) * sizeof(TkCell),
                        8));
        if (song.cells == nullptr && total_cells != 0) return false;
        for (hcpp::u32 i = 0; i < total_cells; ++i) song.cells[i] = TkCell{};
        return true;
    }

    hcpp::u32 tk_simulate(TkSong &song, hcpp::u32 rate) noexcept {
        Eng e;
        eng_init(e, song, rate);
        hcpp::u32 frames = 0;
        while (!e.done) {
            if (e.tick_left == 0) advance(e);
            if (e.done) break;
            frames += e.tick_left;
            e.tick_left = 0;
        }
        song.rows_played = e.rows;
        song.duration_ms =
            static_cast<hcpp::u32>(static_cast<hcpp::u64>(frames) * 1000 / rate);
        return frames;
    }

    hcpp::u32 tk_render(const TkSong &song, hcpp::i16 *dst, hcpp::u32 max_frames,
                        hcpp::u32 rate) noexcept {
        return tk_run(song, dst, max_frames, rate, true);
    }

}  // namespace hype::codec
