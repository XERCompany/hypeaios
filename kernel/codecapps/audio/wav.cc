#include "kernel/codecapps/audio/wav.h"

#include "hcpp/memory.hpp"
#include "kernel/audio/audioapi.h"
#include "kernel/audio/audioparse.h"
#include "kernel/codecapps/arena.h"

namespace hype::codec {

    CodecStatus audio_wav_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                                 Arena &arena) noexcept {
        auto *buf = static_cast<PcmBuf *>(output);
        audio::WavInfo info{};
        if (buf == nullptr || input == nullptr || input_size > 0xFFFFFFFFu ||
            !audio::wav_parse(input, static_cast<hcpp::u32>(input_size), info))
            return CodecStatus::BadInput;
        if (info.audio_format_tag != 1 ||
            (info.bits_per_sample != 8 && info.bits_per_sample != 16))
            return CodecStatus::Unsupported;
        const hcpp::u32 frame_bytes = audio::audio_frame_bytes(info.format);
        if (frame_bytes == 0 || info.sample_bytes == 0)
            return CodecStatus::BadInput;
        if (buf->data == nullptr) {
            buf->data = static_cast<hcpp::u8 *>(arena_alloc(arena, info.sample_bytes));
            if (buf->data == nullptr) return CodecStatus::NoSpace;
            buf->capacity = info.sample_bytes;
        }
        if (info.sample_bytes > buf->capacity) return CodecStatus::NoSpace;
        hcpp::copy(buf->data, info.sample_data, info.sample_bytes);
        buf->format = info.format;
        buf->frames = info.sample_bytes / frame_bytes;
        return CodecStatus::Ok;
    }

    bool audio_wav_query_tags(const hcpp::u8 *input, hcpp::size_t input_size,
                              audio::WavTags &tags) noexcept {
        audio::WavInfo info{};
        if (input == nullptr || input_size > 0xFFFFFFFFu ||
            !audio::wav_parse(input, static_cast<hcpp::u32>(input_size), info))
            return false;
        tags = info.tags;
        return true;
    }

}  // namespace hype::codec
