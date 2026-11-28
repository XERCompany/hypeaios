#include "kernel/codecapps/audio/midi.h"

#include "kernel/codecapps/arena.h"

// MIDI SMF decoder + built-in wavetable synthesizer (wave-2).
// Parse: MThd {format,ntrks,division} then MTrk event streams — VLQ
// delta times, running status, note on/off, program change, CC7/CC10,
// pitch bend, meta tempo/end-of-track, sysex skip. All tracks merge
// into a tick-sorted sequence; a tempo map converts ticks to time.
// Synthesis: no SF2 bank — each GM program maps to a wavetable
// oscillator (sine/triangle/square/saw/noise LUTs) with an ADSR-ish
// envelope; channel 10 renders noise percussion. Output is S16 stereo.

extern "C" double sin(double x) noexcept;
extern "C" double pow(double x, double y) noexcept;

namespace hype::codec {

    namespace {

        constexpr hcpp::size_t max_tracks = 64;
        constexpr hcpp::size_t max_events = 65536;
        constexpr hcpp::u32 max_voices = 24;
        constexpr hcpp::u32 tail_ms = 400;

        enum EvType : hcpp::u8 {
            EvTempo = 0,
            EvProg = 1,
            EvVol = 2,
            EvPan = 3,
            EvBend = 4,
            EvOff = 5,
            EvOn = 6,
        };

        struct MidiEvent {
            hcpp::u32 tick;
            hcpp::u8 type;
            hcpp::u8 ch;
            hcpp::u8 a;
            hcpp::u8 b;
            hcpp::u32 aux;  // tempo µs/quarter
        };

        hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
            return (static_cast<hcpp::u32>(p[0]) << 24) |
                   (static_cast<hcpp::u32>(p[1]) << 16) |
                   (static_cast<hcpp::u32>(p[2]) << 8) | p[3];
        }

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        // Reads a variable-length quantity; advances pos. Returns false on
        // malformed (run past end / >4 bytes).
        bool read_vlq(const hcpp::u8 *data, hcpp::size_t size, hcpp::size_t &pos,
                      hcpp::u32 &value) noexcept {
            value = 0;
            for (int i = 0; i < 4; ++i) {
                if (pos >= size) return false;
                const hcpp::u8 b = data[pos++];
                value = (value << 7) | (b & 0x7F);
                if ((b & 0x80) == 0) return true;
            }
            return false;
        }

        // Walks one MTrk body. When `out` is non-null, stores up to `cap`
        // events; `count` always receives the events seen.
        bool parse_track(const hcpp::u8 *data, hcpp::size_t size, MidiEvent *out,
                         hcpp::size_t cap, hcpp::size_t &count) noexcept {
            count = 0;
            hcpp::size_t pos = 0;
            hcpp::u32 tick = 0;
            hcpp::u8 status = 0;
            for (;;) {
                hcpp::u32 delta = 0;
                if (!read_vlq(data, size, pos, delta)) return false;
                tick += delta;
                if (pos >= size) return false;
                hcpp::u8 ev = data[pos];
                if (ev < 0x80) {
                    if (status == 0) return false;
                    ev = status;  // running status: don't consume the byte
                } else {
                    ++pos;
                    if (ev < 0xF0) status = ev;
                }
                const hcpp::u8 kind = ev & 0xF0;
                const hcpp::u8 ch = ev & 0x0F;
                if (kind == 0xF0) {
                    // Meta / sysex / system-common events.
                    if (ev == 0xFF) {
                        if (pos + 1 > size) return false;
                        const hcpp::u8 meta = data[pos++];
                        hcpp::u32 len = 0;
                        if (!read_vlq(data, size, pos, len) || len > size - pos)
                            return false;
                        if (meta == 0x51 && len == 3) {
                            if (out != nullptr && count < cap)
                                out[count] = MidiEvent{tick, EvTempo, 0, 0, 0,
                                                       read_be32(data + pos - 1) & 0xFFFFFF};
                            ++count;
                        }
                        if (meta == 0x2F) return true;  // end of track
                        pos += len;
                    } else if (ev == 0xF0 || ev == 0xF7) {
                        hcpp::u32 len = 0;
                        if (!read_vlq(data, size, pos, len) || len > size - pos)
                            return false;
                        pos += len;
                    } else if (ev == 0xF1 || ev == 0xF3) {
                        if (pos + 1 > size) return false;
                        ++pos;
                    } else if (ev == 0xF2) {
                        if (pos + 2 > size) return false;
                        pos += 2;
                    } else {
                        // F4-F6, F8-FE: no payload.
                    }
                    continue;
                }
                // Channel events: data-byte count depends on the type.
                hcpp::u8 a = 0, b = 0;
                const bool two = kind != 0xC0 && kind != 0xD0;
                if (pos + (two ? 2 : 1) > size) return false;
                a = data[pos++];
                if (two) b = data[pos++];
                if (a > 0x7F || b > 0x7F) return false;
                hcpp::u8 type = 0xFF;
                switch (kind) {
                    case 0x80:
                        type = EvOff;
                        break;
                    case 0x90:
                        type = b != 0 ? EvOn : EvOff;
                        break;
                    case 0xB0:
                        if (a == 7) type = EvVol;
                        if (a == 10) type = EvPan;
                        break;
                    case 0xC0:
                        type = EvProg;
                        break;
                    case 0xE0:
                        type = EvBend;
                        break;
                    default:
                        break;  // aftertouch / channel pressure: ignored
                }
                if (type == 0xFF) continue;
                if (out != nullptr && count < cap)
                    out[count] = MidiEvent{tick, type, ch, a, b, 0};
                ++count;
            }
        }

        struct Parsed {
            MidiEvent *events;   // merged, tick-sorted
            hcpp::size_t count;
            hcpp::size_t total;  // events seen (>= count when truncated)
            hcpp::u16 format;
            hcpp::u16 ntrks;
            hcpp::u16 division;
            hcpp::u32 notes;
            hcpp::u32 end_tick;
            hcpp::u16 channel_mask;
            bool truncated;
        };

        // Full two-pass parse: counts events, allocates from arena, stores
        // per-track runs, k-way merges them by (tick, type).
        bool midi_parse(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                        Parsed &out) noexcept {
            out = Parsed{};
            if (input == nullptr || input_size < 14 ||
                read_be32(input) != 0x4D546864)  // 'MThd'
                return false;
            const hcpp::u32 hlen = read_be32(input + 4);
            if (hlen < 6 || hlen + 8 > input_size) return false;
            out.format = read_be16(input + 8);
            out.ntrks = read_be16(input + 10);
            out.division = read_be16(input + 12);
            if (out.ntrks == 0 || out.ntrks > max_tracks || out.division == 0)
                return false;

            hcpp::size_t pos = 8 + hlen;
            hcpp::size_t track_bytes[max_tracks];
            hcpp::size_t track_counts[max_tracks];
            const hcpp::u8 *track_data[max_tracks];
            hcpp::u32 ntracks = 0;
            // Pass 0+1: locate MTrk chunks, count events.
            hcpp::size_t total = 0;
            while (pos + 8 <= input_size && ntracks < out.ntrks) {
                const hcpp::u32 len = read_be32(input + pos + 4);
                if (len > input_size - pos - 8) return false;
                const hcpp::u8 *data = input + pos + 8;
                if (read_be32(input + pos) == 0x4D54726B) {  // 'MTrk'
                    hcpp::size_t count = 0;
                    if (!parse_track(data, len, nullptr, 0, count)) return false;
                    track_data[ntracks] = data;
                    track_bytes[ntracks] = len;
                    track_counts[ntracks] = count;
                    ++ntracks;
                    total += count;
                }
                pos += 8 + len;
            }
            if (ntracks == 0) return false;
            out.ntrks = static_cast<hcpp::u16>(ntracks);
            out.total = total;
            const hcpp::size_t cap = total > max_events ? max_events : total;
            out.truncated = total > cap;
            if (cap == 0) return true;
            out.events = static_cast<MidiEvent *>(
                arena_alloc(arena, cap * sizeof(MidiEvent), 8));
            hcpp::u32 *heads = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ntracks * sizeof(hcpp::u32), 8));
            hcpp::u32 *limit = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ntracks * sizeof(hcpp::u32), 8));
            MidiEvent *stored = static_cast<MidiEvent *>(
                arena_alloc(arena, cap * sizeof(MidiEvent), 8));
            if (out.events == nullptr || heads == nullptr || limit == nullptr ||
                stored == nullptr)
                return false;
            // Pass 2: store each track's events contiguously in `stored`
            // (bounded at `cap` — extra events are counted, not stored).
            for (hcpp::u32 t = 0; t < ntracks; ++t) {
                heads[t] = 0;
                limit[t] = 0;
            }
            hcpp::size_t offset = 0;
            for (hcpp::u32 t = 0; t < ntracks && offset < cap; ++t) {
                const hcpp::size_t room = cap - offset;
                hcpp::size_t count = 0;
                if (!parse_track(track_data[t], track_bytes[t], stored + offset,
                                 room, count))
                    return false;
                const hcpp::size_t kept =
                    count < room ? count : room;
                heads[t] = static_cast<hcpp::u32>(offset);
                limit[t] = static_cast<hcpp::u32>(offset + kept);
                offset += kept;
            }
            // K-way merge by (tick, type) — offs order before ons at equal
            // ticks so notes shut cleanly.
            hcpp::size_t wrote = 0;
            while (wrote < cap) {
                hcpp::u32 best = 0;
                bool have = false;
                hcpp::u32 best_tick = 0;
                hcpp::u8 best_type = 0;
                for (hcpp::u32 t = 0; t < ntracks; ++t) {
                    if (heads[t] >= limit[t]) continue;
                    const MidiEvent &e = stored[heads[t]];
                    if (!have || e.tick < best_tick ||
                        (e.tick == best_tick && e.type < best_type)) {
                        best_tick = e.tick;
                        best_type = e.type;
                        best = t;
                        have = true;
                    }
                }
                if (!have) break;
                out.events[wrote++] = stored[heads[best]++];
            }
            for (hcpp::size_t i = 0; i < wrote; ++i) {
                if (out.events[i].type == EvOn) {
                    ++out.notes;
                    out.channel_mask |= static_cast<hcpp::u16>(1u << out.events[i].ch);
                }
                if (out.events[i].tick > out.end_tick)
                    out.end_tick = out.events[i].tick;
            }
            out.count = wrote;
            return true;
        }

        // ---- wavetable synthesizer ------------------------------------

        constexpr hcpp::size_t sine_lut_size = 256;
        hcpp::i16 sine_lut[sine_lut_size];
        bool sine_lut_ready = false;

        void sine_lut_build() noexcept {
            for (hcpp::size_t i = 0; i < sine_lut_size; ++i)
                sine_lut[i] = static_cast<hcpp::i16>(
                    32767.0 * sin(6.283185307179586 * i / sine_lut_size));
            sine_lut_ready = true;
        }

        enum Wave : hcpp::u8 { WSine, WTri, WSquare, WSaw, WNoise };

        // GM program (0-127) -> oscillator family. Honest wavetable
        // mapping: pianos/flutes/choir = sine-triangle, organs/reeds =
        // square, guitars/bass/brass/leads = saw, pads = triangle,
        // percussion & effects = noise.
        hcpp::u8 program_wave(hcpp::u8 program) noexcept {
            if (program < 8) return WTri;      // acoustic pianos & keys
            if (program < 16) return WSine;    // chromatic percussion
            if (program < 24) return WSquare;  // organs
            if (program < 32) return WSaw;     // guitars
            if (program < 40) return WSquare;  // basses
            if (program < 48) return WSaw;     // strings
            if (program < 56) return WSine;    // choir / ensemble
            if (program < 64) return WSaw;     // brass
            if (program < 72) return WSquare;  // reeds
            if (program < 80) return WSine;    // pipes & flutes
            if (program < 88) return WSaw;     // synth leads
            if (program < 96) return WTri;     // synth pads
            if (program < 104) return WNoise;  // synth fx
            if (program < 112) return WTri;    // ethnic
            if (program < 120) return WNoise;  // percussive
            return WNoise;                     // sound effects
        }

        struct Voice {
            hcpp::u8 note;
            hcpp::u8 ch;
            hcpp::u8 vel;
            hcpp::u8 wave;
            hcpp::u8 stage;  // 0 attack, 1 decay, 2 sustain, 3 release, 4 free
            hcpp::u8 pad[3];
            hcpp::u32 phase;
            hcpp::u32 step;
            hcpp::i32 env;   // Q15 amplitude 0..32768
            hcpp::u32 rng;   // noise LFSR state
            hcpp::u32 age;   // for voice stealing
        };

        hcpp::u32 note_step(hcpp::u8 note, double bend_semis, hcpp::u32 rate) noexcept {
            const double freq =
                440.0 * pow(2.0, (static_cast<double>(note) - 69.0 + bend_semis) / 12.0);
            const double step = freq * 4294967296.0 / rate;
            return static_cast<hcpp::u32>(step);
        }

        hcpp::i16 osc(Voice &v) noexcept {
            const hcpp::u8 idx = static_cast<hcpp::u8>(v.phase >> 24);
            switch (v.wave) {
                case WSine:
                    return sine_lut[idx];
                case WTri: {
                    const hcpp::u32 q = v.phase >> 16;  // 0..65535
                    const hcpp::i32 s =
                        q < 32768 ? static_cast<hcpp::i32>(q) * 2 - 32768
                                  : (65535 - static_cast<hcpp::i32>(q)) * 2 - 32768;
                    return static_cast<hcpp::i16>(s);
                }
                case WSquare:
                    return idx < 128 ? 24576 : -24576;
                case WSaw:
                    return static_cast<hcpp::i16>(static_cast<hcpp::i32>(v.phase >> 16) -
                                                  32768);
                default: {
                    // xorshift32 noise (percussion / fx)
                    v.rng ^= v.rng << 13;
                    v.rng ^= v.rng >> 17;
                    v.rng ^= v.rng << 5;
                    return static_cast<hcpp::i16>((v.rng >> 16) & 0xFFFF);
                }
            }
        }

        // One tick of the envelope FSM, per-sample at `rate` Hz.
        void env_step(Voice &v, hcpp::u32 rate) noexcept {
            switch (v.stage) {
                case 0:  // attack ~5 ms
                    v.env += static_cast<hcpp::i32>(32768 * 200 / rate) + 1;
                    if (v.env >= 32768) {
                        v.env = 32768;
                        v.stage = v.wave == WNoise ? 1 : 2;
                    }
                    break;
                case 1:  // percussive decay ~350 ms
                    v.env -= static_cast<hcpp::i32>(32768 * 3 / rate) + 1;
                    if (v.env <= 0) {
                        v.env = 0;
                        v.stage = 4;
                    }
                    break;
                case 2:
                    // decay to 70% sustain over ~100 ms
                    if (v.env > 22937) {
                        v.env -= static_cast<hcpp::i32>(32768 * 10 / rate) + 1;
                        if (v.env < 22937) v.env = 22937;
                    }
                    break;
                case 3:  // release ~120 ms
                    v.env -= static_cast<hcpp::i32>(32768 * 8 / rate) + 1;
                    if (v.env <= 0) {
                        v.env = 0;
                        v.stage = 4;
                    }
                    break;
                default:
                    break;
            }
        }

        double ticks_to_us(hcpp::u16 division, hcpp::u32 tempo_us) noexcept {
            if ((division & 0x8000) != 0) {
                // SMPTE: high byte = -frames/sec, low = ticks per frame.
                const int fps = -static_cast<hcpp::i8>(division >> 8);
                const int tpf = division & 0xFF;
                if (fps <= 0 || tpf == 0) return 0.0;
                return 1000000.0 / (static_cast<double>(fps) * tpf);
            }
            if (division == 0) return 0.0;
            return static_cast<double>(tempo_us) / division;
        }

        CodecStatus midi_render(const Parsed &song, PcmBuf *buf,
                                Arena &arena) noexcept {
            if (!sine_lut_ready) sine_lut_build();
            const hcpp::u32 rate = midi_render_rate;
            // Compute the song length in µs from the tempo map.
            double us_per_tick = ticks_to_us(song.division, 500000);
            double total_us = 0;
            hcpp::u32 last_tick = 0;
            for (hcpp::size_t i = 0; i < song.count; ++i) {
                const MidiEvent &e = song.events[i];
                if (e.tick > last_tick)
                    total_us += (e.tick - last_tick) * us_per_tick;
                last_tick = e.tick;
                if (e.type == EvTempo)
                    us_per_tick = ticks_to_us(song.division, e.aux);
            }
            total_us += tail_ms * 1000.0;
            const double cap_us = static_cast<double>(midi_max_ms) * 1000.0;
            if (total_us > cap_us) total_us = cap_us;
            const hcpp::u64 want_frames =
                static_cast<hcpp::u64>(total_us * rate / 1000000.0) + 1;
            const hcpp::u64 want_bytes = want_frames * 4;
            if (buf->data == nullptr) {
                buf->data = static_cast<hcpp::u8 *>(
                    arena_alloc(arena, static_cast<hcpp::size_t>(want_bytes), 8));
                if (buf->data == nullptr) return CodecStatus::NoSpace;
                buf->capacity = static_cast<hcpp::size_t>(want_bytes);
            }
            const hcpp::u32 frames =
                static_cast<hcpp::u32>(buf->capacity / 4 < want_frames
                                           ? buf->capacity / 4
                                           : want_frames);
            if (frames == 0) return CodecStatus::NoSpace;
            auto *dst = reinterpret_cast<hcpp::i16 *>(buf->data);

            Voice voices[max_voices];
            for (hcpp::u32 i = 0; i < max_voices; ++i) {
                voices[i] = Voice{};
                voices[i].stage = 4;
                voices[i].rng = 0x9E3779B9u + i * 97;
            }
            hcpp::u8 prog[16];
            hcpp::u8 cvol[16];
            hcpp::u8 cpan[16];
            double bend[16];
            for (int c = 0; c < 16; ++c) {
                prog[c] = 0;
                cvol[c] = 100;
                cpan[c] = 64;
                bend[c] = 0.0;
            }

            hcpp::u32 out_i = 0;
            hcpp::u32 cur_tick = 0;
            double pending = 0.0;
            us_per_tick = ticks_to_us(song.division, 500000);
            const auto render_samples = [&](hcpp::u32 count) {
                for (hcpp::u32 s = 0; s < count && out_i < frames; ++s) {
                    hcpp::i32 l = 0, r = 0;
                    for (hcpp::u32 vi = 0; vi < max_voices; ++vi) {
                        Voice &v = voices[vi];
                        if (v.stage == 4) continue;
                        hcpp::i32 sample =
                            (static_cast<hcpp::i32>(osc(v)) * v.env) >> 15;
                        sample = (sample * v.vel) >> 7;
                        sample = (sample * cvol[v.ch]) >> 7;
                        const hcpp::u8 pan = cpan[v.ch];
                        l += (sample * (128 - pan)) >> 7;
                        r += (sample * pan) >> 7;
                        v.phase += v.step;
                        env_step(v, rate);
                        ++v.age;
                    }
                    l >>= 1;
                    r >>= 1;
                    if (l > 32767) l = 32767;
                    if (l < -32768) l = -32768;
                    if (r > 32767) r = 32767;
                    if (r < -32768) r = -32768;
                    dst[out_i * 2] = static_cast<hcpp::i16>(l);
                    dst[out_i * 2 + 1] = static_cast<hcpp::i16>(r);
                    ++out_i;
                }
            };

            for (hcpp::size_t i = 0; i < song.count && out_i < frames; ++i) {
                const MidiEvent &e = song.events[i];
                if (e.tick > cur_tick) {
                    pending += (e.tick - cur_tick) * us_per_tick * rate / 1000000.0;
                    cur_tick = e.tick;
                    const hcpp::u32 whole = static_cast<hcpp::u32>(pending);
                    pending -= whole;
                    render_samples(whole);
                    if (out_i >= frames) break;
                }
                switch (e.type) {
                    case EvOn: {
                        hcpp::u32 slot = max_voices;
                        hcpp::u32 oldest = 0;
                        for (hcpp::u32 vi = 0; vi < max_voices; ++vi) {
                            if (voices[vi].stage == 4) {
                                slot = vi;
                                break;
                            }
                            if (voices[vi].age > voices[oldest].age) oldest = vi;
                        }
                        if (slot == max_voices) slot = oldest;  // steal
                        Voice &v = voices[slot];
                        v.note = e.a;
                        v.ch = e.ch;
                        v.vel = e.b;
                        v.wave = e.ch == 9 ? WNoise : program_wave(prog[e.ch]);
                        v.stage = 0;
                        v.env = 0;
                        v.phase = 0;
                        v.age = 0;
                        v.step = note_step(e.a, bend[e.ch], rate);
                        break;
                    }
                    case EvOff:
                        for (hcpp::u32 vi = 0; vi < max_voices; ++vi)
                            if (voices[vi].stage <= 2 && voices[vi].ch == e.ch &&
                                voices[vi].note == e.a)
                                voices[vi].stage = 3;
                        break;
                    case EvProg:
                        prog[e.ch] = e.a;
                        break;
                    case EvVol:
                        cvol[e.ch] = e.b;
                        break;
                    case EvPan:
                        cpan[e.ch] = e.b;
                        break;
                    case EvBend: {
                        const hcpp::i32 raw =
                            (static_cast<hcpp::i32>(e.b) << 7 | e.a) - 8192;
                        bend[e.ch] = raw * 2.0 / 8192.0;
                        for (hcpp::u32 vi = 0; vi < max_voices; ++vi)
                            if (voices[vi].stage <= 2 && voices[vi].ch == e.ch)
                                voices[vi].step =
                                    note_step(voices[vi].note, bend[e.ch], rate);
                        break;
                    }
                    case EvTempo:
                        us_per_tick = ticks_to_us(song.division, e.aux);
                        break;
                    default:
                        break;
                }
            }
            // Release tail.
            while (out_i < frames) render_samples(frames - out_i);

            buf->format.channels = 2;
            buf->format.sample_rate = rate;
            buf->format.sample_format = audio::AudioSampleFormat::S16Le;
            buf->frames = out_i;
            return CodecStatus::Ok;
        }

    }  // namespace

    bool midi_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                      MidiInfo &info) noexcept {
        Parsed song;
        if (!midi_parse(input, input_size, arena, song)) return false;
        info = MidiInfo{};
        info.format = song.format;
        info.tracks = song.ntrks;
        info.division = song.division;
        info.events = static_cast<hcpp::u32>(song.count);
        info.notes = song.notes;
        info.truncated = song.truncated;
        int channels = 0;
        for (int c = 0; c < 16; ++c)
            if ((song.channel_mask & (1u << c)) != 0) ++channels;
        info.channels = static_cast<hcpp::u16>(channels);
        // Duration in ms through the tempo map.
        double us_per_tick = ticks_to_us(song.division, 500000);
        double us = 0;
        hcpp::u32 last = 0;
        for (hcpp::size_t i = 0; i < song.count; ++i) {
            const MidiEvent &e = song.events[i];
            if (e.tick > last) us += (e.tick - last) * us_per_tick;
            last = e.tick;
            if (e.type == EvTempo) us_per_tick = ticks_to_us(song.division, e.aux);
        }
        us += tail_ms * 1000.0;
        info.duration_ms = static_cast<hcpp::u32>(us / 1000.0);
        return true;
    }

    CodecStatus audio_midi_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr) return CodecStatus::BadInput;
        Parsed song;
        if (!midi_parse(input, input_size, arena, song)) return CodecStatus::BadInput;
        if (song.count == 0 || song.notes == 0) return CodecStatus::BadInput;
        return midi_render(song, buf, arena);
    }

}  // namespace hype::codec
