#include "kernel/codecapps/audio/mod.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/audio/tracker.h"

// ProTracker MOD front-end (wave-2): parses the 31-sample header block,
// order table, channel-count magic at offset 1080 and the packed
// pattern cell stream into a TkSong, then hands it to the shared tick
// engine (tracker.cc).

namespace hype::codec {

    namespace {

        constexpr hcpp::u32 mod_instruments = 31;
        constexpr hcpp::u32 mod_magic_off = 1080;

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        // Returns channel count for a recognized magic, else 0.
        hcpp::u8 mod_channels(const hcpp::u8 *m) noexcept {
            if (m[0] == 'M' && m[1] == '.' && m[2] == 'K' && m[3] == '.') return 4;
            if (m[0] == 'M' && m[1] == '!' && m[2] == 'K' && m[3] == '!') return 4;
            if (m[0] == 'F' && m[1] == 'L' && m[2] == 'T' && m[3] == '4') return 4;
            if (m[0] == 'F' && m[1] == 'L' && m[2] == 'T' && m[3] == '8') return 8;
            if (m[0] >= '1' && m[0] <= '9' && m[1] == 'C' && m[2] == 'H' &&
                m[3] == 'N')
                return static_cast<hcpp::u8>(m[0] - '0');
            if (m[0] >= '0' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' &&
                m[2] == 'C' && m[3] == 'H')
                return static_cast<hcpp::u8>((m[0] - '0') * 10 + (m[1] - '0'));
            return 0;
        }

        bool mod_parse(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                       TkSong &song) noexcept {
            if (input == nullptr || input_size < mod_magic_off + 4) return false;
            const hcpp::u8 channels = mod_channels(input + mod_magic_off);
            if (channels == 0 || channels > tk_max_channels) return false;
            song = TkSong{};
            song.channels = channels;
            song.num_samples = 0;
            song.tempo = 6;
            song.bpm = 125;
            song.restart = 0;

            // 31 sample headers: name22, len/2, finetune, volume,
            // loop_off/2, loop_len/2 — lengths in 16-bit words.
            for (hcpp::u32 i = 0; i < mod_instruments; ++i) {
                const hcpp::u8 *h = input + 20 + i * 30;
                TkSample &s = song.samples[i];
                s.data = nullptr;
                s.length = static_cast<hcpp::u32>(read_be16(h + 22)) * 2;
                s.finetune = static_cast<hcpp::i8>(h[24] & 0x0F);
                if (s.finetune > 7) s.finetune -= 16;
                s.volume = h[25] > 64 ? 64 : h[25];
                s.loop_off = static_cast<hcpp::u32>(read_be16(h + 26)) * 2;
                s.loop_len = static_cast<hcpp::u32>(read_be16(h + 28)) * 2;
            }
            song.num_samples = mod_instruments;

            song.order_count = input[950];
            if (song.order_count == 0 || song.order_count > 128) return false;
            const hcpp::u8 restart = input[951];
            song.restart = restart < song.order_count ? restart : 0;
            hcpp::u8 max_pat = 0;
            for (hcpp::u32 i = 0; i < song.order_count; ++i) {
                song.order[i] = input[952 + i];
                if (song.order[i] > max_pat) max_pat = song.order[i];
            }
            song.num_patterns = max_pat + 1;
            if (song.num_patterns > tk_max_patterns) return false;

            // Pattern data: num_patterns × 64 rows × channels × 4 bytes.
            const hcpp::u64 pat_cells =
                static_cast<hcpp::u64>(song.num_patterns) * 64 * channels;
            const hcpp::u64 pat_bytes = pat_cells * 4;
            const hcpp::size_t pat_start = mod_magic_off + 4;
            if (pat_bytes > input_size - pat_start) return false;

            for (hcpp::u32 p = 0; p < song.num_patterns; ++p) {
                song.pat_rows[p] = 64;
                song.pat_off[p] = p * 64 * channels;
            }
            if (!tk_cells_alloc(song, static_cast<hcpp::u32>(pat_cells), arena))
                return false;
            const hcpp::u8 *cells = input + pat_start;
            for (hcpp::u64 i = 0; i < pat_cells; ++i) {
                const hcpp::u8 *c = cells + i * 4;
                TkCell &cell = song.cells[i];
                cell.period = static_cast<hcpp::u16>(
                    ((c[0] & 0x0F) << 8) | c[1]);
                cell.sample = static_cast<hcpp::u8>((c[0] & 0xF0) | (c[2] >> 4));
                cell.fx = c[2] & 0x0F;
                cell.param = c[3];
            }

            // Sample data follows the patterns, in header order.
            hcpp::size_t pos = pat_start + static_cast<hcpp::size_t>(pat_bytes);
            hcpp::u8 used = 0;
            for (hcpp::u32 i = 0; i < mod_instruments; ++i) {
                TkSample &s = song.samples[i];
                if (s.length == 0) continue;
                if (s.length > input_size - pos) return false;
                s.data = reinterpret_cast<const hcpp::i8 *>(input + pos);
                pos += s.length;
                if (s.loop_len > 0 && s.loop_off + s.loop_len > s.length)
                    s.loop_len = s.loop_off < s.length ? s.length - s.loop_off : 0;
                ++used;
            }
            song.num_samples = mod_instruments;
            (void)used;
            return true;
        }

        CodecStatus mod_output(const TkSong &song, hcpp::u32 frames,
                               PcmBuf *buf, Arena &arena) noexcept {
            const hcpp::u64 want = static_cast<hcpp::u64>(frames) * 4;
            if (want > 0x7FFFFFFFu) return CodecStatus::NoSpace;
            if (buf->data == nullptr) {
                buf->data = static_cast<hcpp::u8 *>(
                    arena_alloc(arena, static_cast<hcpp::size_t>(want), 8));
                if (buf->data == nullptr) return CodecStatus::NoSpace;
                buf->capacity = static_cast<hcpp::size_t>(want);
            }
            const hcpp::u32 cap = static_cast<hcpp::u32>(buf->capacity / 4);
            const hcpp::u32 n = tk_render(song, reinterpret_cast<hcpp::i16 *>(buf->data),
                                          cap < frames ? cap : frames,
                                          mod_render_rate);
            if (n == 0) return CodecStatus::BadInput;
            buf->format.channels = 2;
            buf->format.sample_rate = mod_render_rate;
            buf->format.sample_format = audio::AudioSampleFormat::S16Le;
            buf->frames = n;
            return CodecStatus::Ok;
        }

    }  // namespace

    bool mod_inspect(const hcpp::u8 *input, hcpp::size_t input_size, Arena &arena,
                     ModInfo &info) noexcept {
        TkSong song;
        if (!mod_parse(input, input_size, arena, song)) return false;
        info = ModInfo{};
        info.channels = song.channels;
        info.song_length = static_cast<hcpp::u8>(song.order_count);
        info.patterns = song.num_patterns;
        hcpp::u8 used = 0;
        for (hcpp::u32 i = 0; i < song.num_samples; ++i)
            if (song.samples[i].length != 0) ++used;
        info.instruments = used;
        const hcpp::u32 frames = tk_simulate(song, mod_render_rate);
        info.rows = song.rows_played;
        info.duration_ms = song.duration_ms;
        info.looped = false;
        (void)frames;
        return true;
    }

    CodecStatus audio_mod_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                 void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr) return CodecStatus::BadInput;
        TkSong song;
        if (!mod_parse(input, input_size, arena, song)) return CodecStatus::BadInput;
        const hcpp::u32 frames = tk_simulate(song, mod_render_rate);
        if (frames == 0 || frames > mod_render_rate * (mod_max_ms / 1000))
            return CodecStatus::BadInput;
        return mod_output(song, frames, buf, arena);
    }

}  // namespace hype::codec
