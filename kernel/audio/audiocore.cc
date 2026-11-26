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
                const hcpp::u16 bits = read16(offset + 22);
                info.format.sample_format = bits == 16 ? AudioSampleFormat::S16Le
                                            : bits == 8  ? AudioSampleFormat::U8
                                                         : AudioSampleFormat::Float32;
                fmt_seen = true;
            } else if (tag == audio_tag("data") && fmt_seen) {
                info.sample_data = bytes + offset + 8;
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

    void alac_decoder_reset(AlacDecoderState &state) noexcept { state.frames_decoded = 0; }

    hcpp::u32 alac_decoder_decode(AlacDecoderState &state, const void *packet, hcpp::u32 packet_bytes,
                                  hcpp::i16 *output, hcpp::u32 max_frames) noexcept {
        (void)packet;
        (void)packet_bytes;
        (void)output;
        (void)max_frames;
        if (!state.initialized) {
            return 0;
        }
        return state.frames_decoded;
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
        (void)codec_address;
        return card_index < max_audio_cards;
    }

    const AudioFunctionNode *audiofudev_nodes(hcpp::u8 card_index, hcpp::u8 codec_address,
                                              hcpp::u8 &count) noexcept {
        (void)codec_address;
        count = card_index < max_audio_cards ? function_node_counts[card_index] : 0;
        return card_index < max_audio_cards ? &function_nodes[card_index * audiofudev_max_nodes] : nullptr;
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

}  // namespace hype::audio
