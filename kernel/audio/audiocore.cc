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

    namespace {
        constexpr hcpp::u32 alac_max_element_frames = 4096;
        constexpr hcpp::u8 alac_max_lpc_order = 32;
        hcpp::i32 alac_work[2][alac_max_element_frames];

        hcpp::u32 alac_bits_peek(const AlacBitReader &reader, hcpp::u8 count) noexcept {
            AlacBitReader copy = reader;
            return alac_bits_read(copy, count);
        }

        hcpp::u8 alac_log2(hcpp::u32 value) noexcept {
            hcpp::u8 result = 0;
            while (value > 1) {
                value >>= 1;
                ++result;
            }
            return result;
        }

        hcpp::i32 alac_sign_extend(hcpp::i32 value, hcpp::u8 bits) noexcept {
            if (bits == 0 || bits >= 32) return value;
            const hcpp::u32 shifted = static_cast<hcpp::u32>(value) << (32 - bits);
            return static_cast<hcpp::i32>(shifted) >> (32 - bits);
        }

        hcpp::i32 alac_sign(hcpp::i32 value) noexcept {
            return value > 0 ? 1 : value < 0 ? -1 : 0;
        }

        hcpp::u32 alac_decode_scalar(AlacBitReader &reader, hcpp::u8 k,
                                     hcpp::u8 bps) noexcept {
            hcpp::u32 x = 0;
            while (x <= 8 && !reader.overflow) {
                if (alac_bits_read(reader, 1) != 0) {
                    ++x;
                } else {
                    break;
                }
            }
            if (reader.overflow) return 0;
            if (x > 8) return alac_bits_read(reader, bps);
            if (k != 1) {
                const hcpp::u32 extrabits = alac_bits_peek(reader, k);
                x = (x << k) - x;
                if (extrabits > 1) {
                    x += extrabits - 1;
                    alac_bits_read(reader, k);
                } else {
                    alac_bits_read(reader, k - 1);
                }
            }
            return x;
        }

        bool alac_rice_decompress(AlacBitReader &reader, hcpp::i32 *out, hcpp::u32 count,
                                  hcpp::u8 bps, hcpp::u32 history_mult,
                                  const AlacConfig &config) noexcept {
            hcpp::u32 history = config.initial_history;
            hcpp::u32 sign_modifier = 0;
            hcpp::u32 i = 0;
            while (i < count && !reader.overflow) {
                hcpp::u8 k = alac_log2((history >> 9) + 3);
                if (k > config.rice_modifier) k = config.rice_modifier;
                hcpp::u32 x = alac_decode_scalar(reader, k, bps);
                if (reader.overflow) break;
                x += sign_modifier;
                sign_modifier = 0;
                out[i] = static_cast<hcpp::i32>((x >> 1) ^ (0u - (x & 1)));
                if (x > 0xFFFF) {
                    history = 0xFFFF;
                } else {
                    history += x * history_mult - ((history * history_mult) >> 9);
                }
                if (history < 128 && i + 1 < count) {
                    hcpp::i32 kz = 7 - alac_log2(history) +
                                   static_cast<hcpp::i32>((history + 16) >> 6);
                    if (kz < 0) kz = 0;
                    const hcpp::u8 block_k =
                        kz > config.rice_modifier ? config.rice_modifier
                                                  : static_cast<hcpp::u8>(kz);
                    const hcpp::u32 block = alac_decode_scalar(reader, block_k, 16);
                    if (reader.overflow) break;
                    if (block > 0) {
                        hcpp::u32 run = block;
                        if (run >= count - i) run = count - i - 1;
                        for (hcpp::u32 z = 0; z < run; ++z) out[i + 1 + z] = 0;
                        i += run;
                    }
                    if (block <= 0xFFFF) sign_modifier = 1;
                    history = 0;
                }
                ++i;
            }
            return !reader.overflow;
        }

        void alac_lpc_predict(hcpp::i32 *buffer, hcpp::u32 count, hcpp::u8 bps,
                              hcpp::i16 *coefs, hcpp::u8 order,
                              hcpp::u8 quant) noexcept {
            if (count <= 1 || order == 0) return;
            hcpp::u32 i = 1;
            if (order == 31) {
                for (; i < count; ++i)
                    buffer[i] = alac_sign_extend(buffer[i - 1] + buffer[i], bps);
                return;
            }
            for (; i <= order && i < count; ++i)
                buffer[i] = alac_sign_extend(buffer[i - 1] + buffer[i], bps);
            for (; i < count; ++i) {
                const hcpp::i32 base = buffer[i - order - 1];
                hcpp::i64 val = 0;
                for (hcpp::u8 j = 0; j < order; ++j)
                    val += static_cast<hcpp::i64>(buffer[i - order + j] - base) * coefs[j];
                val = (val + (1LL << (quant - 1))) >> quant;
                const hcpp::i32 error = buffer[i];
                buffer[i] = alac_sign_extend(static_cast<hcpp::i32>(val + base + error),
                                             bps);
                hcpp::i32 residual = error;
                const hcpp::i32 error_sign = alac_sign(residual);
                if (error_sign != 0) {
                    for (hcpp::u8 j = 0;
                         j < order &&
                         static_cast<hcpp::i32>(static_cast<hcpp::u32>(residual) *
                                                static_cast<hcpp::u32>(error_sign)) > 0;
                         ++j) {
                        hcpp::i32 delta = base - buffer[i - order + j];
                        const hcpp::i32 sign = alac_sign(delta) * error_sign;
                        coefs[j] = static_cast<hcpp::i16>(coefs[j] - sign);
                        delta *= sign;
                        residual = static_cast<hcpp::i32>(
                            static_cast<hcpp::u32>(residual) -
                            ((static_cast<hcpp::u32>(delta) >> quant) * (j + 1u)));
                    }
                }
            }
        }
    }  // namespace

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
            if (element_channels > state.config.channels ||
                frames + element_frames > max_frames) {
                state.status = AlacDecodeStatus::Unsupported;
                break;
            }
            if (is_not_compressed != 0) {
                if (uncompressed_bytes != 0) {
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
            } else {
                if (uncompressed_bytes != 0 || element_frames > alac_max_element_frames ||
                    element_channels > 2) {
                    state.status = AlacDecodeStatus::Unsupported;
                    break;
                }
                const hcpp::u32 decorr_shift = alac_bits_read(reader, 8);
                const hcpp::u32 decorr_weight = alac_bits_read(reader, 8);
                hcpp::i16 coefs[2][alac_max_lpc_order];
                hcpp::u8 lpc_order[2]{};
                hcpp::u8 lpc_quant[2]{};
                hcpp::u8 rice_history[2]{};
                hcpp::u8 prediction_type[2]{};
                bool bad = false;
                for (hcpp::u8 c = 0; c < element_channels && !bad; ++c) {
                    prediction_type[c] =
                        static_cast<hcpp::u8>(alac_bits_read(reader, 4));
                    lpc_quant[c] = static_cast<hcpp::u8>(alac_bits_read(reader, 4));
                    rice_history[c] = static_cast<hcpp::u8>(alac_bits_read(reader, 3));
                    lpc_order[c] = static_cast<hcpp::u8>(alac_bits_read(reader, 5));
                    if (lpc_quant[c] == 0 || lpc_order[c] > alac_max_lpc_order) {
                        bad = true;
                        break;
                    }
                    for (hcpp::i8 j = static_cast<hcpp::i8>(lpc_order[c]) - 1; j >= 0; --j)
                        coefs[c][j] = static_cast<hcpp::i16>(
                            static_cast<hcpp::u16>(alac_bits_read(reader, 16)));
                }
                if (bad || reader.overflow) {
                    state.status = reader.overflow ? AlacDecodeStatus::BadPacket
                                                 : AlacDecodeStatus::Unsupported;
                    break;
                }
                const hcpp::u8 bps = static_cast<hcpp::u8>(
                    state.config.bit_depth + element_channels - 1);
                for (hcpp::u8 c = 0; c < element_channels; ++c) {
                    const hcpp::u32 mult =
                        static_cast<hcpp::u32>(rice_history[c]) *
                        state.config.history_multiplier / 4;
                    if (!alac_rice_decompress(reader, alac_work[c], element_frames, bps,
                                              mult, state.config)) {
                        bad = true;
                        break;
                    }
                    if (prediction_type[c] == 15)
                        alac_lpc_predict(alac_work[c], element_frames, bps, nullptr, 31, 0);
                    alac_lpc_predict(alac_work[c], element_frames, bps, coefs[c],
                                     lpc_order[c], lpc_quant[c]);
                }
                if (bad) {
                    state.status = AlacDecodeStatus::BadPacket;
                    break;
                }
                if (element_channels == 2 && decorr_weight != 0) {
                    for (hcpp::u32 i = 0; i < element_frames; ++i) {
                        const hcpp::i32 mid = alac_work[0][i];
                        const hcpp::i32 side = alac_work[1][i];
                        const hcpp::i32 right =
                            mid - static_cast<hcpp::i32>(
                                      (static_cast<hcpp::i64>(side) * decorr_weight) >>
                                      decorr_shift);
                        alac_work[0][i] = side + right;
                        alac_work[1][i] = right;
                    }
                }
                for (hcpp::u32 i = 0; i < element_frames; ++i) {
                    for (hcpp::u8 c = 0; c < state.config.channels; ++c) {
                        const hcpp::i32 sample =
                            alac_work[c < element_channels ? c : 0][i];
                        output[(frames + i) * state.config.channels + c] =
                            static_cast<hcpp::i16>(
                                alac_sign_extend(sample, state.config.bit_depth));
                    }
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
        hda::IzauController controller{};
        hcpp::u32 vendor_id = 0;
        if (!hda::izau_bind(card_index, controller) || !hda::izau_reset(controller) ||
            !hda::izau_codec_verb(controller, codec_address, 0, hda::verb_get_parameter,
                                  hda::param_vendor_id, vendor_id) ||
            vendor_id == 0 || vendor_id == 0xFFFFFFFF) {
            return false;
        }
        AudioFunctionNode *nodes = &function_nodes[card_index * audiofudev_max_nodes];
        for (hcpp::u8 i = 0; i < 2; ++i) {
            nodes[i] = {};
            nodes[i].card_index = card_index;
            nodes[i].codec_address = codec_address;
            nodes[i].node_id = i;
            nodes[i].vendor_id = vendor_id;
        }
        nodes[1].function_group = hda::function_group_audio;
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
