#include "kernel/codecapps/audio/mp3.h"
#include "kernel/audio/audioapi.h"
#include "kernel/codecapps/arena.h"
#include "third_party/minimp3/minimp3.h"

namespace hype::codec {

    namespace {

        hcpp::size_t mp3_skip_id3(const hcpp::u8 *input, hcpp::size_t size) noexcept {
            if (size < 10 || input[0] != 'I' || input[1] != 'D' || input[2] != '3')
                return 0;
            const hcpp::size_t tag = (static_cast<hcpp::size_t>(input[6] & 0x7F) << 21) |
                                     (static_cast<hcpp::size_t>(input[7] & 0x7F) << 14) |
                                     (static_cast<hcpp::size_t>(input[8] & 0x7F) << 7) |
                                     static_cast<hcpp::size_t>(input[9] & 0x7F);
            hcpp::size_t offset = 10 + tag;
            if ((input[5] & 0x10) != 0) offset += 10;
            return offset;
        }

    }  // namespace

    CodecStatus audio_mp3_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                 void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr || input == nullptr || input_size < 8)
            return CodecStatus::BadInput;
        if (buf->data == nullptr) {
            buf->data = static_cast<hcpp::u8 *>(
                arena_alloc(arena, 2 * MINIMP3_MAX_SAMPLES_PER_FRAME * 8));
            if (buf->data == nullptr) return CodecStatus::NoSpace;
            buf->capacity = 2 * MINIMP3_MAX_SAMPLES_PER_FRAME * 8;
        }
        hcpp::size_t offset = mp3_skip_id3(input, input_size);
        if (offset >= input_size) return CodecStatus::BadInput;
        static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        static mp3dec_t decoder;
        mp3dec_init(&decoder);
        auto *out = reinterpret_cast<hcpp::i16 *>(buf->data);
        hcpp::size_t samples_total = 0;
        hcpp::u32 rate = 0;
        hcpp::u8 channels = 0;
        bool any = false;
        while (offset + 4 <= input_size) {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(
                &decoder, input + offset, static_cast<int>(input_size - offset), pcm, &info);
            if (info.frame_bytes <= 0) {
                ++offset;
                continue;
            }
            offset += static_cast<hcpp::size_t>(info.frame_bytes);
            if (samples <= 0 || info.channels <= 0) continue;
            if (rate == 0) {
                rate = static_cast<hcpp::u32>(info.hz);
                channels = static_cast<hcpp::u8>(info.channels);
            }
            if (static_cast<hcpp::u32>(info.hz) != rate ||
                info.channels != static_cast<int>(channels))
                continue;
            const hcpp::size_t count =
                static_cast<hcpp::size_t>(samples) * static_cast<hcpp::size_t>(channels);
            if ((samples_total + count) * 2 > buf->capacity) return CodecStatus::NoSpace;
            for (hcpp::size_t i = 0; i < count; ++i) {
                const hcpp::i16 sample = pcm[i];
                out[samples_total + i] = sample;
                if (sample != 0) any = true;
            }
            samples_total += count;
        }
        if (samples_total == 0 || rate == 0 || channels == 0 || !any)
            return CodecStatus::BadInput;
        buf->format.sample_rate = rate;
        buf->format.channels = channels;
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = samples_total / channels;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
