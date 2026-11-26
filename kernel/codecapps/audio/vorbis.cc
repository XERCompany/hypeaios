#include "kernel/codecapps/audio/vorbis.h"
#include "kernel/audio/audioapi.h"
#include "kernel/codecapps/arena.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_COMMENTS
#include "third_party/stb/stb_vorbis.h"

namespace hype::codec {

    namespace {
        constexpr hcpp::size_t vorbis_work_max = 1 << 20;
        constexpr hcpp::size_t vorbis_work_min = 1 << 16;
    }

    CodecStatus audio_vorbis_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                    void *output, Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr || input == nullptr || input_size < 27)
            return CodecStatus::BadInput;
        if (buf->data == nullptr || buf->capacity < 2) return CodecStatus::NoSpace;
        hcpp::size_t work = vorbis_work_max;
        char *memory = nullptr;
        while (memory == nullptr && work >= vorbis_work_min) {
            memory = static_cast<char *>(arena_alloc(arena, work, 16));
            if (memory == nullptr) work >>= 1;
        }
        if (memory == nullptr) return CodecStatus::NoSpace;
        stb_vorbis_alloc alloc{};
        alloc.alloc_buffer = memory;
        alloc.alloc_buffer_length_in_bytes = static_cast<int>(work);
        int error = 0;
        stb_vorbis *decoder =
            stb_vorbis_open_memory(input, static_cast<int>(input_size), &error, &alloc);
        if (decoder == nullptr)
            return error == VORBIS_outofmem ? CodecStatus::NoSpace : CodecStatus::BadInput;
        const stb_vorbis_info info = stb_vorbis_get_info(decoder);
        if (info.channels <= 0 || info.channels > 2 || info.sample_rate == 0) {
            stb_vorbis_close(decoder);
            return CodecStatus::BadInput;
        }
        auto *out = reinterpret_cast<hcpp::i16 *>(buf->data);
        const hcpp::size_t channels = static_cast<hcpp::size_t>(info.channels);
        const hcpp::size_t frame_cap = buf->capacity / (2 * channels);
        hcpp::size_t total = 0;
        for (;;) {
            const int got = stb_vorbis_get_frame_short_interleaved(
                decoder, info.channels, out + total * channels,
                static_cast<int>((frame_cap - total) * channels));
            if (got <= 0) break;
            total += static_cast<hcpp::size_t>(got);
            if (total == frame_cap) {
                stb_vorbis_close(decoder);
                return CodecStatus::NoSpace;
            }
        }
        stb_vorbis_close(decoder);
        if (total == 0) return CodecStatus::BadInput;
        bool any = false;
        for (hcpp::size_t i = 0; i < total * channels; ++i)
            if (out[i] != 0) {
                any = true;
                break;
            }
        if (!any) return CodecStatus::BadInput;
        buf->format.sample_rate = info.sample_rate;
        buf->format.channels = static_cast<hcpp::u8>(info.channels);
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = total;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
