#include "kernel/codecapps/audio/mod.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/audio/tracker.h"
#include "kernel/codecapps/image/imageutil.h"

// XM (FastTracker II "Extended Module") front-end (wave-2): parses the
// header, packed patterns and instrument/sample tables into a TkSong
// and reuses the shared tick engine. Honest subset: envelopes,
// multi-sample keymaps (first sample plays), most volume-column
// commands and XM-only effects (G..X) are skipped — the core effect
// letters shared with MOD all play.

namespace hype::codec {

    namespace {

        using image::read_le16;
        using image::read_le32;

        struct XmHeader {
            hcpp::u16 song_len;
            hcpp::u16 restart;
            hcpp::u16 channels;
            hcpp::u16 patterns;
            hcpp::u16 instruments;
            hcpp::u16 flags;
            hcpp::u16 tempo;
            hcpp::u16 bpm;
            hcpp::size_t pos;  // first pattern offset
        };

        bool xm_header(const hcpp::u8 *input, hcpp::size_t n, XmHeader &h,
                       hcpp::u8 *order) noexcept {
            if (n < 64) return false;
            for (int i = 0; i < 17; ++i)
                if (input[i] != static_cast<hcpp::u8>("Extended Module: "[i]))
                    return false;
            if (input[37] != 0x1A) return false;
            // header_size counts from the size dword itself (includes it):
            // 4 + 16 bytes of u16 fields + 256-byte order table = 276.
            const hcpp::u32 hsize = read_le32(input + 60);
            if (hsize < 20 || hsize > 276 || 60 + hsize > n) return false;
            h.song_len = read_le16(input + 64);
            h.restart = read_le16(input + 66);
            h.channels = read_le16(input + 68);
            h.patterns = read_le16(input + 70);
            h.instruments = read_le16(input + 72);
            h.flags = read_le16(input + 74);
            h.tempo = read_le16(input + 76);
            h.bpm = read_le16(input + 78);
            if (h.song_len == 0 || h.song_len > 256 || h.channels == 0 ||
                h.channels > tk_max_channels || h.patterns > tk_max_patterns)
                return false;
            const hcpp::size_t orders = 80;
            for (hcpp::u32 i = 0; i < h.song_len && i < 256; ++i) {
                if (orders + i >= 60 + hsize) return false;
                order[i] = input[orders + i];
            }
            h.pos = 60 + hsize;
            return true;
        }

        // One packed pattern -> TkCell grid. Returns cells written, or 0
        // on malformed data. `cells` has room for rows*channels entries.
        bool xm_pattern(const hcpp::u8 *input, hcpp::size_t n, hcpp::size_t &pos,
                        hcpp::u16 channels, TkCell *cells, hcpp::u16 &rows) noexcept {
            if (pos + 9 > n) return false;
            const hcpp::u32 hlen = read_le32(input + pos);
            if (hlen < 9 || hlen > 64 || pos + hlen > n) return false;
            rows = read_le16(input + pos + 5);
            const hcpp::u32 packed = read_le16(input + pos + 7);
            hcpp::size_t data = pos + hlen;
            pos = data + packed;
            if (pos > n) return false;
            if (rows == 0 || rows > 256) return false;
            const hcpp::size_t body_end = data + packed;
            hcpp::size_t out = 0;
            const hcpp::size_t total =
                static_cast<hcpp::size_t>(rows) * channels;
            while (out < total && data < body_end) {
                TkCell cell{};
                hcpp::u8 b = input[data++];
                if ((b & 0x80) != 0) {
                    if ((b & 0x01) != 0 && data < body_end) cell.note = input[data++];
                    if ((b & 0x02) != 0 && data < body_end)
                        cell.sample = input[data++];
                    if ((b & 0x04) != 0 && data < body_end) cell.vol = input[data++];
                    if ((b & 0x08) != 0 && data < body_end) cell.fx = input[data++];
                    if ((b & 0x10) != 0 && data < body_end)
                        cell.param = input[data++];
                } else {
                    cell.note = b;
                    if (data + 4 > body_end) return false;
                    cell.sample = input[data++];
                    cell.vol = input[data++];
                    cell.fx = input[data++];
                    cell.param = input[data++];
                }
                cells[out++] = cell;
            }
            return out == total;
        }

        // Parses instruments + their samples. Sample payload is delta-
        // decoded into `arena` (16-bit data is reduced to s8 honestly).
        // `inst_base` maps instrument number -> flat sample index so cells
        // can be rewritten after parsing.
        bool xm_instruments(const hcpp::u8 *input, hcpp::size_t n, hcpp::size_t &pos,
                            hcpp::u16 instruments, TkSong &song, Arena &arena,
                            bool decode_data, hcpp::u8 *inst_base) noexcept {
            for (hcpp::u32 inst = 0; inst < instruments; ++inst) {
                inst_base[inst] = 0xFF;
                if (pos + 29 > n) return false;
                const hcpp::u32 isize = read_le32(input + pos);
                if (isize < 29 || pos + isize > n) return false;
                const hcpp::u16 nsamp = read_le16(input + pos + 27);
                if (nsamp == 0) {
                    pos += isize;
                    continue;
                }
                if (isize < 33) return false;
                const hcpp::u32 shsize = read_le32(input + pos + 29);
                if (shsize < 40) return false;
                hcpp::size_t sh = pos + isize;
                if (sh + static_cast<hcpp::size_t>(nsamp) * shsize > n) return false;
                inst_base[inst] = static_cast<hcpp::u8>(song.num_samples);
                if (song.num_samples + nsamp > tk_max_samples) return false;
                hcpp::size_t data = sh + static_cast<hcpp::size_t>(nsamp) * shsize;
                for (hcpp::u32 s = 0; s < nsamp; ++s) {
                    const hcpp::u8 *h = input + sh + s * shsize;
                    TkSample &ts = song.samples[song.num_samples];
                    const hcpp::u32 slen = read_le32(h);
                    ts.loop_off = read_le32(h + 4);
                    ts.loop_len = read_le32(h + 8);
                    ts.volume = h[12] > 64 ? 64 : h[12];
                    ts.finetune = static_cast<hcpp::i8>(
                        static_cast<hcpp::i8>(h[13]) / 16);
                    const hcpp::u8 type = h[14];
                    ts.relnote = static_cast<hcpp::i8>(h[16]);
                    const bool wide = (type & 0x10) != 0;
                    const hcpp::u32 samples_len = wide ? slen / 2 : slen;
                    if (wide) {
                        // XM loop points are byte offsets too.
                        ts.loop_off /= 2;
                        ts.loop_len /= 2;
                    }
                    ts.length = samples_len;
                    if ((type & 0x03) == 0) ts.loop_len = 0;
                    if (ts.loop_off >= ts.length ||
                        ts.loop_len > ts.length - ts.loop_off)
                        ts.loop_len = 0;
                    if (decode_data && slen != 0) {
                        if (data + slen > n) return false;
                        hcpp::i8 *pcm = static_cast<hcpp::i8 *>(
                            arena_alloc(arena, samples_len != 0 ? samples_len : 1, 8));
                        if (pcm == nullptr) return false;
                        if (!wide) {
                            hcpp::i8 acc = 0;
                            for (hcpp::u32 k = 0; k < slen; ++k) {
                                acc = static_cast<hcpp::i8>(acc +
                                                          static_cast<hcpp::i8>(
                                                              input[data + k]));
                                pcm[k] = acc;
                            }
                        } else {
                            hcpp::i16 acc = 0;
                            for (hcpp::u32 k = 0; k < samples_len; ++k) {
                                acc = static_cast<hcpp::i16>(
                                    acc + static_cast<hcpp::i16>(
                                              read_le16(input + data + k * 2)));
                                pcm[k] = static_cast<hcpp::i8>(acc >> 8);
                            }
                        }
                        ts.data = pcm;
                    }
                    ++song.num_samples;
                    data += slen;
                }
                pos = data;
            }
            return true;
        }

        bool xm_parse(const hcpp::u8 *input, hcpp::size_t n, Arena &arena,
                      TkSong &song, bool decode_data) noexcept {
            song = TkSong{};
            XmHeader h{};
            if (!xm_header(input, n, h, song.order)) return false;
            song.channels = static_cast<hcpp::u8>(h.channels);
            song.tempo = h.tempo != 0 ? static_cast<hcpp::u8>(h.tempo) : 6;
            song.bpm = h.bpm != 0 ? h.bpm : 125;
            song.order_count = h.song_len;
            song.restart = h.restart < h.song_len ? static_cast<hcpp::u8>(h.restart) : 0;
            song.num_patterns = static_cast<hcpp::u8>(h.patterns);
            // Two passes over patterns: rows/offsets then cell fill.
            hcpp::size_t pos = h.pos;
            hcpp::u32 total_cells = 0;
            hcpp::u16 rows[tk_max_patterns];
            for (hcpp::u32 p = 0; p < h.patterns; ++p) {
                if (pos + 9 > n) return false;
                const hcpp::u32 hlen = read_le32(input + pos);
                const hcpp::u32 packed = read_le16(input + pos + 7);
                rows[p] = read_le16(input + pos + 5);
                if (hlen < 9 || pos + hlen + packed > n || rows[p] == 0 ||
                    rows[p] > 256)
                    return false;
                song.pat_rows[p] = rows[p];
                song.pat_off[p] = total_cells;
                total_cells += static_cast<hcpp::u32>(rows[p]) * h.channels;
                pos += hlen + packed;
            }
            if (!tk_cells_alloc(song, total_cells, arena)) return false;
            pos = h.pos;
            for (hcpp::u32 p = 0; p < h.patterns; ++p) {
                hcpp::u16 r = 0;
                if (!xm_pattern(input, n, pos, static_cast<hcpp::u16>(h.channels),
                                song.cells + song.pat_off[p], r))
                    return false;
            }
            hcpp::u8 inst_base[128];
            for (hcpp::u32 i = 0; i < 128; ++i) inst_base[i] = 0xFF;
            if (h.instruments > 128) return false;
            if (!xm_instruments(input, n, pos, h.instruments, song, arena,
                                decode_data, inst_base))
                return false;
            // Rewrite instrument numbers in cells to flat sample indexes
            // (each instrument maps to its first sample).
            for (hcpp::u32 i = 0; i < total_cells; ++i) {
                TkCell &cell = song.cells[i];
                if (cell.sample == 0) continue;
                const hcpp::u8 inst = cell.sample - 1;
                if (inst < h.instruments && inst_base[inst] != 0xFF)
                    cell.sample = inst_base[inst] + 1;
                else
                    cell.sample = 0;
            }
            return true;
        }

        CodecStatus xm_output(const TkSong &song, hcpp::u32 frames, PcmBuf *buf,
                              Arena &arena) noexcept {
            const hcpp::u64 want = static_cast<hcpp::u64>(frames) * 4;
            if (want > 0x7FFFFFFFu) return CodecStatus::NoSpace;
            if (buf->data == nullptr) {
                buf->data = static_cast<hcpp::u8 *>(
                    arena_alloc(arena, static_cast<hcpp::size_t>(want), 8));
                if (buf->data == nullptr) return CodecStatus::NoSpace;
                buf->capacity = static_cast<hcpp::size_t>(want);
            }
            const hcpp::u32 cap = static_cast<hcpp::u32>(buf->capacity / 4);
            const hcpp::u32 n =
                tk_render(song, reinterpret_cast<hcpp::i16 *>(buf->data),
                          cap < frames ? cap : frames, mod_render_rate);
            if (n == 0) return CodecStatus::BadInput;
            buf->format.channels = 2;
            buf->format.sample_rate = mod_render_rate;
            buf->format.sample_format = audio::AudioSampleFormat::S16Le;
            buf->frames = n;
            return CodecStatus::Ok;
        }

    }  // namespace

    bool xm_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                    XmInfo &info) noexcept {
        TkSong song;
        if (!xm_parse(input, input_size, arena, song, false)) return false;
        info = XmInfo{};
        info.channels = song.channels;
        info.patterns = song.num_patterns;
        info.song_length = song.order_count;
        info.flags = 0;
        info.rows_total = 0;
        for (hcpp::u32 p = 0; p < song.num_patterns; ++p)
            info.rows_total += song.pat_rows[p];
        info.instruments = 0;
        info.has_samples = song.num_samples != 0;
        // instruments count = header field; recover via the raw header.
        XmHeader h{};
        hcpp::u8 order[256];
        if (xm_header(input, input_size, h, order)) {
            info.instruments = h.instruments;
            info.flags = static_cast<hcpp::u8>(h.flags & 1);
        }
        return true;
    }

    CodecStatus audio_xm_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr) return CodecStatus::BadInput;
        TkSong song;
        if (!xm_parse(input, input_size, arena, song, true))
            return CodecStatus::BadInput;
        const hcpp::u32 frames = tk_simulate(song, mod_render_rate);
        if (frames == 0 || frames > mod_render_rate * (mod_max_ms / 1000))
            return CodecStatus::BadInput;
        return xm_output(song, frames, buf, arena);
    }

}  // namespace hype::codec
