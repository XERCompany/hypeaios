#include "kernel/codecapps/audio/adpcm.h"
#include "kernel/audio/audioapi.h"
#include "kernel/audio/audioparse.h"
#include "kernel/codecapps/arena.h"

namespace hype::codec {

    namespace {

        constexpr hcpp::i16 ima_step_table[89] = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
            19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
            50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
            130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
            337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
            876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
            2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
            5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
            15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
        };

        constexpr hcpp::i8 ima_index_table[16] = {
            -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
        };

        hcpp::i16 g711_mulaw_table[256];
        hcpp::i16 g711_alaw_table[256];
        bool g711_tables_ready;

        void g711_build_tables() noexcept {
            for (hcpp::size_t i = 0; i < 256; ++i) {
                const hcpp::u8 u = static_cast<hcpp::u8>(~i);
                hcpp::i32 t = ((u & 0x0F) << 3) + 0x84;
                t <<= (u >> 4) & 0x07;
                g711_mulaw_table[i] = static_cast<hcpp::i16>((u & 0x80) ? (0x84 - t)
                                                                      : (t - 0x84));
                const hcpp::u8 a = static_cast<hcpp::u8>(i ^ 0x55);
                const hcpp::i32 seg = (a >> 4) & 0x07;
                hcpp::i32 v = (a & 0x0F) << 4;
                if (seg == 0) {
                    v += 8;
                } else {
                    v = (v + 0x108) << (seg - 1);
                }
                g711_alaw_table[i] = static_cast<hcpp::i16>((a & 0x80) ? v : -v);
            }
            g711_tables_ready = true;
        }

        hcpp::u16 rd16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>(p[0] | (static_cast<hcpp::u16>(p[1]) << 8));
        }

        hcpp::u32 rd32(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }

        struct WavFields {
            hcpp::u16 tag;
            hcpp::u16 channels;
            hcpp::u32 rate;
            hcpp::u16 block_align;
            const hcpp::u8 *data;
            hcpp::size_t data_size;
        };

        bool wav_fields(const hcpp::u8 *bytes, hcpp::size_t size, WavFields &out) noexcept {
            if (bytes == nullptr || size < 20 || rd32(bytes) != audio::audio_tag("RIFF") ||
                rd32(bytes + 8) != audio::audio_tag("WAVE"))
                return false;
            bool fmt_seen = false;
            hcpp::size_t offset = 12;
            while (offset + 8 <= size) {
                const hcpp::u32 tag = rd32(bytes + offset);
                const hcpp::u32 chunk = rd32(bytes + offset + 4);
                if (chunk > size || offset + 8 + chunk > size) break;
                if (tag == audio::audio_tag("fmt ") && chunk >= 16) {
                    out.tag = rd16(bytes + offset + 8);
                    out.channels = rd16(bytes + offset + 10);
                    out.rate = rd32(bytes + offset + 12);
                    out.block_align = rd16(bytes + offset + 20);
                    fmt_seen = true;
                } else if (tag == audio::audio_tag("data") && fmt_seen) {
                    out.data = bytes + offset + 8;
                    out.data_size = chunk;
                    return true;
                }
                offset += 8 + chunk + (chunk & 1);
            }
            return false;
        }

        hcpp::i16 ima_nibble(hcpp::u8 nibble, hcpp::i32 &predictor,
                             hcpp::i32 &index) noexcept {
            const hcpp::i32 step = ima_step_table[index];
            hcpp::i32 delta = step >> 3;
            if ((nibble & 0x04) != 0) delta += step;
            if ((nibble & 0x02) != 0) delta += step >> 1;
            if ((nibble & 0x01) != 0) delta += step >> 2;
            predictor += (nibble & 0x08) != 0 ? -delta : delta;
            if (predictor > 32767) predictor = 32767;
            if (predictor < -32768) predictor = -32768;
            index += ima_index_table[nibble & 0x0F];
            if (index < 0) index = 0;
            if (index > 88) index = 88;
            return static_cast<hcpp::i16>(predictor);
        }

        hcpp::size_t ima_block(const hcpp::u8 *block, hcpp::size_t block_bytes,
                               hcpp::u16 channels, hcpp::i16 *out,
                               hcpp::size_t out_frames_cap) noexcept {
            if (block_bytes < 4 * static_cast<hcpp::size_t>(channels) || out == nullptr)
                return 0;
            hcpp::i32 predictor[2];
            hcpp::i32 index[2];
            for (hcpp::u16 c = 0; c < channels; ++c) {
                predictor[c] = static_cast<hcpp::i16>(rd16(block + 4 * c));
                index[c] = block[4 * c + 2];
                if (index[c] > 88 || block[4 * c + 3] != 0) index[c] = 0;
                out[c] = static_cast<hcpp::i16>(predictor[c]);
            }
            hcpp::size_t frames = 1;
            hcpp::size_t pos = 4 * channels;
            while (pos < block_bytes) {
                for (hcpp::u16 c = 0; c < channels; ++c) {
                    for (hcpp::u8 byte = 0; byte < 4 && pos < block_bytes;
                         ++byte, ++pos) {
                        const hcpp::u8 packed = block[pos];
                        const hcpp::u8 nibbles[2] = {
                            static_cast<hcpp::u8>(packed & 0x0F),
                            static_cast<hcpp::u8>(packed >> 4)};
                        for (hcpp::u8 nib = 0; nib < 2; ++nib) {
                            const hcpp::size_t frame = frames + byte * 2 + nib;
                            if (frame >= out_frames_cap) return frames;
                            out[frame * channels + c] =
                                ima_nibble(nibbles[nib], predictor[c], index[c]);
                        }
                    }
                }
                frames += 8;
            }
            return frames;
        }

        CodecStatus decode_g711(const WavFields &wav, PcmBuf &buf,
                                const hcpp::i16 *table) noexcept {
            const hcpp::size_t needed = wav.data_size * 2;
            if (needed > buf.capacity) return CodecStatus::NoSpace;
            auto *out = reinterpret_cast<hcpp::i16 *>(buf.data);
            for (hcpp::size_t i = 0; i < wav.data_size; ++i)
                out[i] = table[wav.data[i]];
            buf.frames = wav.data_size / wav.channels;
            return CodecStatus::Ok;
        }

        CodecStatus decode_ima(const WavFields &wav, PcmBuf &buf) noexcept {
            if (wav.block_align < 4 * wav.channels || wav.data_size < wav.block_align)
                return CodecStatus::BadInput;
            const hcpp::size_t frame_cap = buf.capacity / 2 / wav.channels;
            const hcpp::size_t block_frames =
                (wav.block_align - 4 * wav.channels) * 2 / wav.channels + 1;
            auto *out = reinterpret_cast<hcpp::i16 *>(buf.data);
            hcpp::size_t total = 0;
            for (hcpp::size_t pos = 0; pos + wav.block_align <= wav.data_size;
                 pos += wav.block_align) {
                if (block_frames > frame_cap - total) return CodecStatus::NoSpace;
                const hcpp::size_t got = ima_block(wav.data + pos, wav.block_align,
                                                   wav.channels, out + total * wav.channels,
                                                   frame_cap - total);
                if (got == 0) return CodecStatus::BadInput;
                total += got;
            }
            if (total == 0) return CodecStatus::BadInput;
            buf.frames = total;
            return CodecStatus::Ok;
        }

    }  // namespace

    CodecStatus audio_adpcm_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                   void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        WavFields wav{};
        if (buf == nullptr || !wav_fields(input, input_size, wav) ||
            wav.channels == 0 || wav.channels > 2)
            return CodecStatus::BadInput;
        if (buf->data == nullptr) {
            const hcpp::size_t worst = wav.data_size * 4 + 64;
            buf->data = static_cast<hcpp::u8 *>(arena_alloc(arena, worst));
            if (buf->data == nullptr) return CodecStatus::NoSpace;
            buf->capacity = worst;
        }
        buf->format.sample_rate = wav.rate;
        buf->format.channels = static_cast<hcpp::u8>(wav.channels);
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = 0;
        switch (wav.tag) {
            case 0x11:
                return decode_ima(wav, *buf);
            case 0x06:
            case 0x07:
                if (!g711_tables_ready) g711_build_tables();
                return decode_g711(wav, *buf, wav.tag == 0x06 ? g711_alaw_table
                                                            : g711_mulaw_table);
            default:
                return CodecStatus::Unsupported;
        }
    }

}  // namespace hype::codec
