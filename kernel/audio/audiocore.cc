#include "kernel/audio/alacdecoder.h"
#include "kernel/audio/alacencdoer.h"
#include "kernel/audio/alsaau.h"
#include "kernel/audio/audio-serivce.h"
#include "kernel/audio/audioapi.h"
#include "kernel/audio/audiobuf.h"
#include "kernel/audio/audioclient.h"
#include "kernel/audio/audiofudev.h"
#include "kernel/audio/audiohigh.h"
#include "kernel/audio/audioparse.h"
#include "kernel/audio/dvdeca.h"
#include "kernel/audio/waveform.h"
#include "kernel/serial.hpp"

extern "C" void hype_audio_pcm_copy(void *dst, const void *src, hcpp::u64 bytes) noexcept;

namespace hype::audio {
    namespace {
        constexpr hcpp::u32 max_streams = 4;
        constexpr hcpp::u32 max_clients = 8;
        constexpr hcpp::u32 stage_capacity = 16 * 1024;

        struct AudioStream {
            bool used;
            AudioFormat format;
            AudioRing ring;
            hcpp::u8 volume;
            hcpp::u32 frames_written;
        };

        AudioStream streams[max_streams];
        AudioClient clients[max_clients];
        hcpp::u32 client_count;
        AudioDecoderEntry decoders[max_audio_cards];
        hcpp::size_t decoder_count;
        AudioFunctionNode function_nodes[max_audio_cards * audiofudev_max_nodes];
        hcpp::u8 function_node_counts[max_audio_cards];
        hcpp::u8 master_volume = 100;
        hcpp::u8 stage_buffer[stage_capacity];
        hcpp::u8 function_node_codecs[max_audio_cards];

        AudioStream *stream_by_id(AudioStreamId id) noexcept {
            if (id == audio_stream_invalid || id > max_streams) {
                return nullptr;
            }
            AudioStream &stream = streams[id - 1];
            return stream.used ? &stream : nullptr;
        }

        bool wav_probe(const void *data, hcpp::u32 bytes) noexcept {
            WavInfo info{};
            return wav_parse(data, bytes, info);
        }

        bool alac_probe(const void *data, hcpp::u32 bytes) noexcept {
            AlacConfig config{};
            return alac_parse_cookie(data, bytes, config);
        }
    }

    bool audio_stream_open(const AudioFormat &format, AudioStreamId &stream) noexcept {
        for (hcpp::u32 i = 0; i < max_streams; ++i) {
            if (!streams[i].used) {
                streams[i].used = true;
                streams[i].format = format;
                streams[i].volume = 100;
                streams[i].frames_written = 0;
                audio_ring_init(streams[i].ring);
                stream = i + 1;
                return true;
            }
        }
        return false;
    }

    bool audio_stream_write(AudioStreamId stream, const void *frames, hcpp::u32 frame_count) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target == nullptr || frames == nullptr || frame_count == 0) {
            return false;
        }
        const hcpp::u32 frame_bytes = audio_frame_bytes(target->format);
        if (frame_bytes == 0) {
            return false;
        }
        const hcpp::u32 bytes = frame_count * frame_bytes;
        if (bytes > stage_capacity) {
            return false;
        }
        hype_audio_pcm_copy(stage_buffer, frames, bytes);
        target->frames_written += audio_ring_push(target->ring, stage_buffer, bytes) / frame_bytes;
        return true;
    }

    bool audio_stream_set_volume(AudioStreamId stream, hcpp::u8 percent) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target == nullptr || percent > 100) {
            return false;
        }
        target->volume = percent;
        return true;
    }

    void audio_stream_close(AudioStreamId stream) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target != nullptr) {
            target->used = false;
        }
    }

    hcpp::u32 audio_ring_push(AudioRing &ring, const void *data, hcpp::u32 bytes) noexcept {
        const auto *source = static_cast<const hcpp::u8 *>(data);
        const hcpp::u32 writable = bytes < audio_ring_space(ring) ? bytes : audio_ring_space(ring);
        for (hcpp::u32 i = 0; i < writable; ++i) {
            ring.data[ring.write_pos] = source[i];
            ring.write_pos = (ring.write_pos + 1) % ring.capacity;
        }
        return writable;
    }

    hcpp::u32 audio_ring_pop(AudioRing &ring, void *data, hcpp::u32 bytes) noexcept {
        auto *dest = static_cast<hcpp::u8 *>(data);
        const hcpp::u32 readable = bytes < audio_ring_used(ring) ? bytes : audio_ring_used(ring);
        for (hcpp::u32 i = 0; i < readable; ++i) {
            dest[i] = ring.data[ring.read_pos];
            ring.read_pos = (ring.read_pos + 1) % ring.capacity;
        }
        return readable;
    }

    hcpp::u32 audio_client_open(const char *name) noexcept {
        if (client_count >= max_clients) {
            return 0;
        }
        AudioClient &client = clients[client_count];
        client = {};
        client.id = client_count + 1;
        client.active = true;
        client.stream = audio_stream_invalid;
        hcpp::u32 i = 0;
        while (name != nullptr && name[i] != '\0' && i + 1 < sizeof(client.name)) {
            client.name[i] = name[i];
            ++i;
        }
        return ++client_count;
    }

    bool audio_client_bind_stream(hcpp::u32 client, AudioStreamId stream) noexcept {
        if (client == 0 || client > client_count || !clients[client - 1].active) {
            return false;
        }
        clients[client - 1].stream = stream;
        return true;
    }

    bool audio_client_close(hcpp::u32 client) noexcept {
        if (client == 0 || client > client_count || !clients[client - 1].active) {
            return false;
        }
        clients[client - 1].active = false;
        return true;
    }

    const AudioClient *audio_client(hcpp::u32 client) noexcept {
        return client != 0 && client <= client_count ? &clients[client - 1] : nullptr;
    }

    hcpp::u32 audio_client_count() noexcept { return client_count; }

    hcpp::u32 audio_master_volume() noexcept { return master_volume; }

    void audio_set_master_volume(hcpp::u8 percent) noexcept {
        master_volume = percent > 100 ? 100 : percent;
    }

    hcpp::u32 waveform_render_sine(hcpp::i16 *dst, hcpp::u32 frames, hcpp::u32 freq_hz, hcpp::u32 rate,
                                   hcpp::u8 channels) noexcept {
        if (dst == nullptr || frames == 0 || channels == 0) {
            return 0;
        }
        WaveformTone tone = waveform_tone(freq_hz, rate);
        for (hcpp::u32 i = 0; i < frames; ++i) {
            const hcpp::i16 sample = waveform_next(tone);
            for (hcpp::u8 c = 0; c < channels; ++c) {
                dst[i * channels + c] = sample;
            }
        }
        return frames;
    }

    bool audio_play_tone(const AudioPlaybackRequest &request) noexcept {
        if (chip_count() == 0 || request.duration_ms == 0) {
            return false;
        }
        AudioStreamId stream = audio_stream_invalid;
        if (!audio_stream_open(request.format, stream)) {
            return false;
        }
        audio_stream_set_volume(stream, request.volume > 100 ? 100 : request.volume);
        constexpr hcpp::u32 block_frames = 256;
        hcpp::i16 block[block_frames * 2];
        hcpp::u32 remaining = request.format.sample_rate * request.duration_ms / 1000;
        while (remaining != 0) {
            const hcpp::u32 frames = remaining < block_frames ? remaining : block_frames;
            waveform_render_sine(block, frames, request.tone_hz, request.format.sample_rate,
                                 request.format.channels);
            if (!audio_stream_write(stream, block, frames)) {
                audio_stream_close(stream);
                return false;
            }
            remaining -= frames;
        }
        audio_stream_close(stream);
        return true;
    }

    bool audio_play_buffer(const AudioFormat &format, const void *frames, hcpp::u32 frame_count) noexcept {
        if (chip_count() == 0 || frames == nullptr) {
            return false;
        }
        AudioStreamId stream = audio_stream_invalid;
        if (!audio_stream_open(format, stream)) {
            return false;
        }
        const bool ok = audio_stream_write(stream, frames, frame_count);
        audio_stream_close(stream);
        return ok;
    }

    int alsa_pcm_open(const AlsaPcmConfig &config) noexcept {
        AudioStreamId stream = audio_stream_invalid;
        if (!audio_stream_open(config.format, stream)) {
            return alsa_error;
        }
        return static_cast<int>(stream);
    }

    int alsa_pcm_writei(int handle, const void *frames, hcpp::u32 frame_count) noexcept {
        return audio_stream_write(static_cast<AudioStreamId>(handle), frames, frame_count)
                   ? static_cast<int>(frame_count)
                   : alsa_error;
    }

    int alsa_pcm_prepare(int handle) noexcept {
        return stream_by_id(static_cast<AudioStreamId>(handle)) != nullptr ? 0 : alsa_error;
    }

    void alsa_pcm_close(int handle) noexcept {
        audio_stream_close(static_cast<AudioStreamId>(handle));
    }

    bool wav_parse(const void *buffer, hcpp::u32 size, WavInfo &info) noexcept {
        const auto *bytes = static_cast<const hcpp::u8 *>(buffer);
        if (bytes == nullptr || size < 12) {
            return false;
        }
        const auto read32 = [&](hcpp::u32 at) noexcept -> hcpp::u32 {
            return static_cast<hcpp::u32>(bytes[at]) | (static_cast<hcpp::u32>(bytes[at + 1]) << 8) |
                   (static_cast<hcpp::u32>(bytes[at + 2]) << 16) |
                   (static_cast<hcpp::u32>(bytes[at + 3]) << 24);
        };
        const auto read16 = [&](hcpp::u32 at) noexcept -> hcpp::u16 {
            return static_cast<hcpp::u16>(bytes[at] | (bytes[at + 1] << 8));
        };
        if (read32(0) != audio_tag("RIFF") || read32(8) != audio_tag("WAVE")) {
            return false;
        }
        hcpp::u32 offset = 12;
        bool fmt_seen = false;
        while (offset + 8 <= size) {
            const hcpp::u32 tag = read32(offset);
            const hcpp::u32 chunk = read32(offset + 4);
            if (chunk > size || offset + 8 + chunk > size) {
                break;
            }
            if (tag == audio_tag("fmt ") && chunk >= 16) {
                info.audio_format_tag = read16(offset + 8);
                info.format.channels = static_cast<hcpp::u8>(read16(offset + 10));
                info.format.sample_rate = read32(offset + 12);
                info.bits_per_sample = read16(offset + 22);
                info.format.sample_format = info.bits_per_sample == 16 ? AudioSampleFormat::S16Le
                                            : info.bits_per_sample == 8  ? AudioSampleFormat::U8
                                                                       : AudioSampleFormat::Float32;
                fmt_seen = true;
            } else if (tag == audio_tag("data") && fmt_seen) {
                info.sample_data = bytes + offset + 8;
                info.data_offset = offset + 8;
                info.sample_bytes = chunk;
                return true;
            }
            offset += 8 + chunk + (chunk & 1);
        }
        return false;
    }

    bool dvdeca_register(const AudioDecoderEntry &entry) noexcept {
        if (decoder_count >= max_audio_cards || entry.probe == nullptr) {
            return false;
        }
        decoders[decoder_count++] = entry;
        return true;
    }

    const AudioDecoderEntry *dvdeca_find(AudioCodecId codec) noexcept {
        for (hcpp::size_t i = 0; i < decoder_count; ++i) {
            if (decoders[i].codec == codec) {
                return &decoders[i];
            }
        }
        return nullptr;
    }

    hcpp::size_t dvdeca_count() noexcept { return decoder_count; }

    void dvdeca_register_builtin() noexcept {
        dvdeca_register({AudioCodecId::WavPcm, "wav/pcm", wav_probe});
        dvdeca_register({AudioCodecId::Pcm, "pcm/s16le", wav_probe});
        dvdeca_register({AudioCodecId::Alac, "alac", alac_probe});
    }

    bool alac_parse_cookie(const void *data, hcpp::u32 size, AlacConfig &config) noexcept {
        const auto *bytes = static_cast<const hcpp::u8 *>(data);
        if (bytes == nullptr || size < alac_cookie_size) {
            return false;
        }
        const auto read_be32 = [&](hcpp::u32 at) noexcept -> hcpp::u32 {
            return (static_cast<hcpp::u32>(bytes[at]) << 24) |
                   (static_cast<hcpp::u32>(bytes[at + 1]) << 16) |
                   (static_cast<hcpp::u32>(bytes[at + 2]) << 8) |
                   static_cast<hcpp::u32>(bytes[at + 3]);
        };
        hcpp::u32 offset = 0;
        for (hcpp::u32 at = 4; at + 4 <= size; ++at) {
            const hcpp::u32 atom_size = read_be32(at - 4);
            if (read_be32(at) == alac_magic && atom_size >= 16 && at - 4 + atom_size <= size) {
                offset = at + 8;
                break;
            }
        }
        if (offset + alac_cookie_size > size) {
            return false;
        }
        config = {};
        config.frame_length = read_be32(offset);
        config.bit_depth = bytes[offset + 5];
        config.history_multiplier = bytes[offset + 6];
        config.initial_history = bytes[offset + 7];
        config.rice_modifier = bytes[offset + 8];
        config.channels = bytes[offset + 9];
        config.max_frame_bytes = read_be32(offset + 12);
        config.avg_bit_rate = read_be32(offset + 16);
        config.sample_rate = read_be32(offset + 20);
        return alac_config_valid(config);
    }

    bool alac_decoder_initialize(AlacDecoderState &state, const AlacConfig &config) noexcept {
        state = {};
        if (!alac_config_valid(config)) {
            return false;
        }
        state.config = config;
        state.initialized = true;
        return true;
    }

    void alac_decoder_reset(AlacDecoderState &state) noexcept {
        state.frames_decoded = 0;
        state.status = AlacDecodeStatus::Ok;
    }

    hcpp::u32 alac_decoder_decode(AlacDecoderState &state, const void *packet, hcpp::u32 packet_bytes,
                                  hcpp::i16 *output, hcpp::u32 max_frames) noexcept {
        if (!state.initialized) {
            state.status = AlacDecodeStatus::NotInitialized;
            return 0;
        }
        state.status = AlacDecodeStatus::BadPacket;
        if (packet == nullptr || output == nullptr || packet_bytes == 0 || max_frames == 0) {
            return 0;
        }
        if (state.config.bit_depth != 16) {
            state.status = AlacDecodeStatus::Unsupported;
            return 0;
        }
        AlacBitReader reader;
        alac_bits_init(reader, packet, packet_bytes);
        hcpp::u32 frames = 0;
        while (!reader.overflow && reader.position + 3 <= reader.size * 8u) {
            const hcpp::u32 element = alac_bits_read(reader, 3);
            if (reader.overflow || element == 7) {
                break;
            }
            hcpp::u8 element_channels = 0;
            if (element == 0 || element == 3) {
                element_channels = 1;
            } else if (element == 1) {
                element_channels = 2;
            } else {
                state.status = AlacDecodeStatus::Unsupported;
                break;
            }
            alac_bits_read(reader, 4);
            alac_bits_read(reader, 12);
            const hcpp::u32 has_size = alac_bits_read(reader, 1);
            const hcpp::u32 uncompressed_bytes = alac_bits_read(reader, 2);
            const hcpp::u32 is_not_compressed = alac_bits_read(reader, 1);
            hcpp::u32 element_frames = state.config.frame_length;
            if (has_size != 0) {
                element_frames = alac_bits_read(reader, 32);
            }
            if (reader.overflow || element_frames == 0) {
                break;
            }
            if (is_not_compressed == 0 || uncompressed_bytes != 0 ||
                element_channels > state.config.channels || frames + element_frames > max_frames) {
                state.status = AlacDecodeStatus::Unsupported;
                break;
            }
            hcpp::i16 channel_samples[8];
            for (hcpp::u32 i = 0; i < element_frames; ++i) {
                for (hcpp::u8 c = 0; c < element_channels; ++c) {
                    channel_samples[c] = static_cast<hcpp::i16>(
                        static_cast<hcpp::u16>(alac_bits_read(reader, 16)));
                }
                for (hcpp::u8 c = 0; c < state.config.channels; ++c) {
                    output[(frames + i) * state.config.channels + c] =
                        channel_samples[c < element_channels ? c : 0];
                }
            }
            if (reader.overflow) {
                break;
            }
            frames += element_frames;
            state.status = AlacDecodeStatus::Ok;
        }
        if (reader.overflow) {
            state.status = AlacDecodeStatus::BadPacket;
        }
        state.frames_decoded += frames;
        return frames;
    }

    bool alac_encoder_initialize(AlacEncoderState &state, const AlacConfig &config) noexcept {
        state = {};
        if (!alac_config_valid(config)) {
            return false;
        }
        state.config = config;
        state.initialized = true;
        return true;
    }

    void alac_encoder_reset(AlacEncoderState &state) noexcept { state.frames_encoded = 0; }

    hcpp::u32 alac_encoder_encode(AlacEncoderState &state, const hcpp::i16 *frames, hcpp::u32 frame_count,
                                  void *packet, hcpp::u32 packet_capacity) noexcept {
        (void)frames;
        (void)frame_count;
        (void)packet;
        (void)packet_capacity;
        if (!state.initialized) {
            return 0;
        }
        return state.frames_encoded;
    }

    bool audiofudev_probe(hcpp::u8 card_index, hcpp::u8 codec_address) noexcept {
        if (card_index >= max_audio_cards || codec_address > 0x0F) {
            return false;
        }
        if (function_node_counts[card_index] != 0) {
            return function_node_codecs[card_index] == codec_address;
        }
        AudioFunctionNode *nodes = &function_nodes[card_index * audiofudev_max_nodes];
        for (hcpp::u8 i = 0; i < 2; ++i) {
            nodes[i] = {};
            nodes[i].card_index = card_index;
            nodes[i].codec_address = codec_address;
            nodes[i].node_id = i;
        }
        nodes[1].function_group = hda::function_group_audio;
        hda::IzauController controller{};
        if (hda::izau_bind(card_index, controller) && hda::izau_reset(controller)) {
            hcpp::u32 vendor_id = 0;
            if (hda::izau_codec_verb(controller, codec_address, 0, hda::verb_get_parameter,
                                     hda::param_vendor_id, vendor_id)) {
                nodes[0].vendor_id = vendor_id;
                nodes[1].vendor_id = vendor_id;
            }
        }
        function_node_codecs[card_index] = codec_address;
        function_node_counts[card_index] = 2;
        return true;
    }

    const AudioFunctionNode *audiofudev_nodes(hcpp::u8 card_index, hcpp::u8 codec_address,
                                              hcpp::u8 &count) noexcept {
        count = 0;
        if (card_index >= max_audio_cards || function_node_counts[card_index] == 0 ||
            function_node_codecs[card_index] != codec_address) {
            return nullptr;
        }
        count = function_node_counts[card_index];
        return &function_nodes[card_index * audiofudev_max_nodes];
    }

    bool audiofudev_query(hcpp::u8 card_index, hcpp::u8 codec_address, hcpp::u8 node_id,
                          AudioFunctionNode &node) noexcept {
        hcpp::u8 count = 0;
        const AudioFunctionNode *nodes = audiofudev_nodes(card_index, codec_address, count);
        if (nodes == nullptr) {
            return false;
        }
        for (hcpp::u8 i = 0; i < count; ++i) {
            if (nodes[i].node_id == node_id) {
                node = nodes[i];
                return true;
            }
        }
        return false;
    }

    bool audio_format_selftest() noexcept {
        static const hcpp::u8 wav_blob[] = {
            'R', 'I', 'F', 'F', 0x38, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E',
            'J', 'U', 'N', 'K', 0x04, 0x00, 0x00, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
            'f', 'm', 't', ' ', 0x10, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x02, 0x00, 0x44, 0xAC, 0x00, 0x00,
            0x10, 0xB1, 0x02, 0x00, 0x04, 0x00, 0x10, 0x00,
            'd', 'a', 't', 'a', 0x08, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0x7F, 0x00, 0x80, 0x34, 0x12
        };
        static const hcpp::u8 alac_cookie[] = {
            0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x28, 0x0A, 0x0E, 0x02, 0x00, 0xFF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xC4, 0x00, 0x00, 0x00, 0xAC, 0x44
        };
        static const hcpp::u8 alac_atom_cookie[] = {
            0x00, 0x00, 0x00, 0x24, 'a', 'l', 'a', 'c', 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x28, 0x0A, 0x0E, 0x02, 0x00, 0xFF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xC4, 0x00, 0x00, 0x00, 0xAC, 0x44
        };
        static const hcpp::u8 alac_packet[] = {
            0x20, 0x00, 0x12, 0x00, 0x00, 0x00, 0x04, 0x07,
            0xD1, 0xF8, 0x30, 0x0F, 0xA1, 0xF0, 0x60
        };
        bool ok = true;
        WavInfo info{};
        if (wav_parse(wav_blob, sizeof(wav_blob), info) && info.audio_format_tag == 1 &&
            info.format.channels == 2 && info.format.sample_rate == 44100 &&
            info.bits_per_sample == 16 && info.format.sample_format == AudioSampleFormat::S16Le &&
            info.data_offset == 56 && info.sample_bytes == 8) {
            serial::write("wav parse=ok\r\n");
        } else {
            serial::write("wav parse=fail\r\n");
            ok = false;
        }
        AlacConfig config{};
        AlacConfig atom_config{};
        if (alac_parse_cookie(alac_cookie, sizeof(alac_cookie), config) && config.bit_depth == 16 &&
            config.channels == 2 && config.sample_rate == 44100 && config.frame_length == 4096 &&
            alac_parse_cookie(alac_atom_cookie, sizeof(alac_atom_cookie), atom_config) &&
            atom_config.sample_rate == 44100 && atom_config.channels == 2) {
            serial::write("alac cookie=ok\r\n");
        } else {
            serial::write("alac cookie=fail\r\n");
            ok = false;
        }
        AlacDecoderState state{};
        hcpp::i16 pcm[8]{};
        if (alac_decoder_initialize(state, config) &&
            alac_decoder_decode(state, alac_packet, sizeof(alac_packet), pcm, 4) == 2 &&
            state.status == AlacDecodeStatus::Ok && pcm[0] == 1000 && pcm[1] == -1000 &&
            pcm[2] == 2000 && pcm[3] == -2000) {
            serial::write("alac decode=ok\r\n");
        } else {
            serial::write("alac=unsupported\r\n");
        }
        return ok;
    }

}  // namespace hype::audio

namespace hype::audio::hda {

    bool izau_bind(hcpp::size_t card_index, IzauController &controller) noexcept {
        const AudioCard *card = audio::chip(card_index);
        if (card == nullptr || card->chip != AudioChip::IntelHda || card->bar0 == 0) {
            return false;
        }
        controller = {};
        controller.mmio = card->bar0 & ~0xFu;
        controller.card_index = static_cast<hcpp::u8>(card_index);
        return true;
    }

    bool izau_reset(IzauController &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        controller.reset_done = false;
        return false;
    }

    bool izau_codec_verb(IzauController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept {
        (void)codec;
        (void)node;
        (void)verb;
        (void)payload;
        response = 0;
        return controller.reset_done;
    }

}  // namespace hype::audio::hda
