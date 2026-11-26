#include "kernel/codecapps/audio/alac_codec.h"
#include "kernel/audio/alacdecoder.h"
#include "kernel/audio/audioapi.h"
#include "kernel/codecapps/arena.h"

namespace hype::codec {

    namespace {

        hcpp::size_t alac_find_cookie_atom(const hcpp::u8 *input, hcpp::size_t size,
                                           hcpp::size_t &atom_end) noexcept {
            for (hcpp::size_t at = 4; at + 4 <= size; ++at) {
                if (input[at] != 'a' || input[at + 1] != 'l' || input[at + 2] != 'a' ||
                    input[at + 3] != 'c')
                    continue;
                const hcpp::size_t atom_size =
                    (static_cast<hcpp::size_t>(input[at - 4]) << 24) |
                    (static_cast<hcpp::size_t>(input[at - 3]) << 16) |
                    (static_cast<hcpp::size_t>(input[at - 2]) << 8) |
                    static_cast<hcpp::size_t>(input[at - 1]);
                if (atom_size < 16 || at - 4 + atom_size > size) continue;
                atom_end = at - 4 + atom_size;
                return at;
            }
            return static_cast<hcpp::size_t>(-1);
        }

    }  // namespace

    CodecStatus audio_alac_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                  void *output, Arena &arena) noexcept {
        (void)arena;
        auto *buf = static_cast<PcmBuf *>(output);
        if (buf == nullptr || input == nullptr || input_size < 40)
            return CodecStatus::BadInput;
        if (buf->data == nullptr || buf->capacity < 4) return CodecStatus::NoSpace;
        audio::AlacConfig config{};
        if (!audio::alac_parse_cookie(input, static_cast<hcpp::u32>(input_size), config))
            return CodecStatus::BadInput;
        hcpp::size_t packet_offset = 0;
        if (alac_find_cookie_atom(input, input_size, packet_offset) ==
            static_cast<hcpp::size_t>(-1))
            return CodecStatus::BadInput;
        if (packet_offset >= input_size) return CodecStatus::BadInput;
        audio::AlacDecoderState state{};
        if (!audio::alac_decoder_initialize(state, config)) return CodecStatus::BadInput;
        auto *out = reinterpret_cast<hcpp::i16 *>(buf->data);
        const hcpp::u32 max_frames =
            static_cast<hcpp::u32>(buf->capacity / (2 * config.channels));
        const hcpp::u32 frames =
            audio::alac_decoder_decode(state, input + packet_offset,
                                       static_cast<hcpp::u32>(input_size - packet_offset),
                                       out, max_frames);
        if (frames == 0 || state.status != audio::AlacDecodeStatus::Ok)
            return CodecStatus::BadInput;
        bool any = false;
        for (hcpp::size_t i = 0; i < static_cast<hcpp::size_t>(frames) * config.channels;
             ++i)
            if (out[i] != 0) {
                any = true;
                break;
            }
        if (!any) return CodecStatus::BadInput;
        buf->format.sample_rate = config.sample_rate;
        buf->format.channels = config.channels;
        buf->format.sample_format = audio::AudioSampleFormat::S16Le;
        buf->frames = frames;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
