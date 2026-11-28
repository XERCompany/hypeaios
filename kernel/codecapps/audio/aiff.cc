#include "kernel/codecapps/audio/aiff.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/image/imageutil.h"

// AIFF decoder (wave-2): FORM container walking + COMM/SSND parsing.
// Sound data is big-endian signed PCM; converted to S16Le PcmBuf.

namespace hype::codec {

    namespace {

        using image::read_be32;

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        bool type_eq(const hcpp::u8 *p, const char *name) noexcept {
            return p[0] == static_cast<hcpp::u8>(name[0]) &&
                   p[1] == static_cast<hcpp::u8>(name[1]) &&
                   p[2] == static_cast<hcpp::u8>(name[2]) &&
                   p[3] == static_cast<hcpp::u8>(name[3]);
        }

        // 80-bit IEEE-754 extended (as used by AIFF COMM sampleRate):
        // 1 sign bit, 15-bit exponent biased 16383, explicit 64-bit mantissa.
        hcpp::u32 ext80_to_u32(const hcpp::u8 *p) noexcept {
            const hcpp::u16 se = read_be16(p);
            if ((se & 0x8000) != 0) return 0;
            const int exp = (se & 0x7FFF) - 16383;
            hcpp::u64 mant = 0;
            for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
            if (exp < 0 || exp > 31) return 0;
            // value = mant * 2^(exp - 63)
            const int shift = 63 - exp;
            const hcpp::u64 value = mant >> shift;
            return value > 0xFFFFFFFFu ? 0 : static_cast<hcpp::u32>(value);
        }

    }  // namespace

    CodecStatus audio_aiff_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (input == nullptr || buf == nullptr || input_size < 12) return CodecStatus::BadInput;
        if (!type_eq(input, "FORM")) return CodecStatus::BadInput;
        const bool aifc = type_eq(input + 8, "AIFC");
        if (!type_eq(input + 8, "AIFF") && !aifc) return CodecStatus::BadInput;

        hcpp::u16 channels = 0;
        hcpp::u32 frames = 0;
        hcpp::u16 bits = 0;
        hcpp::u32 rate = 0;
        bool little = false;
        const hcpp::u8 *pcm = nullptr;
        hcpp::size_t pcm_size = 0;

        hcpp::size_t pos = 12;
        while (pos + 8 <= input_size) {
            const hcpp::u8 *chunk = input + pos;
            const hcpp::u32 length = read_be32(chunk + 4);
            const hcpp::u8 *data = chunk + 8;
            const hcpp::size_t avail = input_size - pos - 8;
            const hcpp::size_t body = length < avail ? length : avail;
            if (type_eq(chunk, "COMM")) {
                if (body < 18) return CodecStatus::BadInput;
                channels = read_be16(data);
                frames = read_be32(data + 2);
                bits = read_be16(data + 6);
                rate = ext80_to_u32(data + 8);
                if (aifc && body >= 22) {
                    little = type_eq(data + 18, "sowt");
                    if (!little && !type_eq(data + 18, "NONE") &&
                        !type_eq(data + 18, "twos"))
                        return CodecStatus::Unsupported;
                }
            } else if (type_eq(chunk, "SSND")) {
                if (body < 8) return CodecStatus::BadInput;
                const hcpp::u32 offset = read_be32(data);
                if (offset > body - 8) return CodecStatus::BadInput;
                pcm = data + 8 + offset;
                pcm_size = body - 8 - offset;
            }
            const hcpp::size_t step = 8 + length + (length & 1);
            if (step < 12) return CodecStatus::BadInput;
            pos += step;
        }

        if (pcm == nullptr || channels == 0 || channels > 8 || rate == 0 ||
            (bits != 8 && bits != 16))
            return CodecStatus::BadInput;
        const hcpp::u32 bytes_per_frame = static_cast<hcpp::u32>(channels) * (bits / 8);
        const hcpp::u64 avail_frames64 =
            bytes_per_frame != 0 ? pcm_size / bytes_per_frame : 0;
        if (avail_frames64 < frames) frames = static_cast<hcpp::u32>(avail_frames64);
        const hcpp::u64 out_bytes64 =
            static_cast<hcpp::u64>(frames) * channels * 2;
        if (frames == 0 || out_bytes64 > 0x7FFFFFFFu) return CodecStatus::BadInput;
        const hcpp::size_t out_bytes = static_cast<hcpp::size_t>(out_bytes64);
        if (buf->data == nullptr) {
            buf->data = static_cast<hcpp::u8 *>(arena_alloc(arena, out_bytes, 8));
            if (buf->data == nullptr) return CodecStatus::NoSpace;
            buf->capacity = out_bytes;
        }
        if (out_bytes > buf->capacity) return CodecStatus::NoSpace;

        auto *dst = reinterpret_cast<hcpp::i16 *>(buf->data);
        const hcpp::size_t total = static_cast<hcpp::size_t>(frames) * channels;
        if (bits == 16 && !little) {
            for (hcpp::size_t i = 0; i < total; ++i)
                dst[i] = static_cast<hcpp::i16>(
                    (static_cast<hcpp::u16>(pcm[i * 2]) << 8) | pcm[i * 2 + 1]);
        } else if (bits == 16 && little) {
            for (hcpp::size_t i = 0; i < total; ++i)
                dst[i] = static_cast<hcpp::i16>(
                    static_cast<hcpp::u16>(pcm[i * 2]) |
                    (static_cast<hcpp::u16>(pcm[i * 2 + 1]) << 8));
        } else if (bits == 8) {
            for (hcpp::size_t i = 0; i < total; ++i)
                dst[i] = static_cast<hcpp::i16>(static_cast<hcpp::i8>(pcm[i]) << 8);
        }

        buf->format.channels = static_cast<hcpp::u8>(channels);
        buf->format.sample_rate = rate;
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = frames;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
