#include "kernel/codecapps/audio/flac.h"
#include "kernel/audio/audioapi.h"
#include "kernel/codecapps/arena.h"
#include "third_party/dr_libs/dr_flac.h"

namespace hype::codec {

    namespace {

        void *flac_arena_malloc(size_t size, void *user) noexcept {
            return arena_alloc(*static_cast<Arena *>(user), size, 16);
        }

        void *flac_arena_realloc(void *pointer, size_t size, void *user) noexcept {
            (void)pointer;
            return arena_alloc(*static_cast<Arena *>(user), size, 16);
        }

        void flac_arena_free(void *pointer, void *user) noexcept {
            (void)pointer;
            (void)user;
        }

    }  // namespace

    CodecStatus audio_flac_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr || input == nullptr || input_size < 42)
            return CodecStatus::BadInput;
        if (buf->data == nullptr || buf->capacity < 2) return CodecStatus::NoSpace;
        drflac_allocation_callbacks callbacks{};
        callbacks.onMalloc = flac_arena_malloc;
        callbacks.onRealloc = flac_arena_realloc;
        callbacks.onFree = flac_arena_free;
        callbacks.pUserData = &arena;
        drflac *decoder = drflac_open_memory(input, input_size, &callbacks);
        if (decoder == nullptr) return CodecStatus::BadInput;
        const hcpp::u32 rate = decoder->sampleRate;
        const hcpp::u32 channels = decoder->channels;
        if (channels == 0 || channels > 8) {
            drflac_close(decoder);
            return CodecStatus::BadInput;
        }
        auto *out = reinterpret_cast<hcpp::i16 *>(buf->data);
        const drflac_uint64 frame_cap = buf->capacity / (2 * channels);
        drflac_uint64 total = drflac_read_pcm_frames_s16(decoder, frame_cap, out);
        drflac_int16 probe_sample[8];
        if (total == frame_cap &&
            drflac_read_pcm_frames_s16(decoder, 1, probe_sample) != 0) {
            drflac_close(decoder);
            return CodecStatus::NoSpace;
        }
        drflac_close(decoder);
        if (total == 0) return CodecStatus::BadInput;
        buf->format.sample_rate = rate;
        buf->format.channels = static_cast<hcpp::u8>(channels);
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = static_cast<hcpp::size_t>(total);
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
