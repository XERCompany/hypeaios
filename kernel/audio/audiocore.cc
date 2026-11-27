#include "kernel/audio/alacdecoder.h"
#include "kernel/audio/alacencdoer.h"
#include "kernel/audio/alsaau.h"
#include "kernel/audio/audio-serivce.h"
#include "kernel/audio/audioapi.h"
#include "kernel/audio/audiobuf.h"
#include "kernel/audio/audioclient.h"
#include "kernel/audio/audiofudev.h"
#include "kernel/audio/audiofx.h"
#include "kernel/audio/audiohigh.h"
#include "kernel/audio/audioparse.h"
#include "kernel/audio/dvdeca.h"
#include "kernel/audio/waveform.h"
#include "kernel/serial.hpp"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

extern "C" void hype_audio_pcm_copy(void *dst, const void *src, hcpp::u64 bytes) noexcept;
extern "C" void hype_audio_buffer_clear(hcpp::i16 *dst, hcpp::u64 frames) noexcept;
extern "C" void hype_audio_mix_s16(hcpp::i16 *dst, const hcpp::i16 *src,
                                   hcpp::u64 frames) noexcept;
extern "C" void hype_audio_pcm_mono_to_stereo_s16(hcpp::i16 *dst, const hcpp::i16 *src,
                                                  hcpp::u64 frames) noexcept;
extern "C" void hype_audio_pcm_swap_channels_s16(hcpp::i16 *stereo, hcpp::u64 frames) noexcept;

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
            hcpp::u16 gain_q8;
            AudioStreamState state;
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
                streams[i].gain_q8 = audio_gain_unity_q8;
                streams[i].state = AudioStreamState::Running;
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
        target->gain_q8 = static_cast<hcpp::u16>(percent * audio_gain_unity_q8 / 100);
        return true;
    }

    bool audio_stream_set_gain(AudioStreamId stream, hcpp::u16 gain_q8) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target == nullptr) {
            return false;
        }
        target->gain_q8 = gain_q8;
        target->volume = static_cast<hcpp::u8>(
            gain_q8 * 100 / audio_gain_unity_q8 > 100 ? 100
                                                    : gain_q8 * 100 / audio_gain_unity_q8);
        return true;
    }

    hcpp::u16 audio_stream_gain(AudioStreamId stream) noexcept {
        const AudioStream *target = stream_by_id(stream);
        return target != nullptr ? target->gain_q8 : 0;
    }

    bool audio_stream_set_state(AudioStreamId stream, AudioStreamState state) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target == nullptr || state == AudioStreamState::Idle) {
            return false;
        }
        target->state = state;
        return true;
    }

    bool audio_stream_info(AudioStreamId stream, AudioStreamInfo &info) noexcept {
        const AudioStream *target = stream_by_id(stream);
        if (target == nullptr) {
            return false;
        }
        info.id = stream;
        info.format = target->format;
        info.gain_q8 = target->gain_q8;
        info.state = target->state;
        info.frames_queued = audio_ring_used_frames(
            target->ring, audio_frame_bytes(target->format));
        info.underruns = audio_ring_underruns(target->ring);
        return true;
    }

    bool audio_stream_info_at(hcpp::u32 slot, AudioStreamInfo &info) noexcept {
        if (slot >= max_streams) {
            return false;
        }
        const AudioStream &entry = streams[slot];
        info.id = slot + 1;
        info.format = entry.format;
        info.gain_q8 = entry.gain_q8;
        info.state = entry.used ? entry.state : AudioStreamState::Idle;
        info.frames_queued =
            entry.used ? audio_ring_used_frames(entry.ring, audio_frame_bytes(entry.format))
                       : 0;
        info.underruns = entry.used ? audio_ring_underruns(entry.ring) : 0;
        return true;
    }

    hcpp::u32 audio_stream_count() noexcept {
        hcpp::u32 count = 0;
        for (hcpp::u32 i = 0; i < max_streams; ++i) {
            if (streams[i].used) {
                ++count;
            }
        }
        return count;
    }

    void audio_stream_close(AudioStreamId stream) noexcept {
        AudioStream *target = stream_by_id(stream);
        if (target != nullptr) {
            target->used = false;
            target->state = AudioStreamState::Idle;
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

    hcpp::u32 audio_ring_push_frames(AudioRing &ring, hcpp::u32 frame_bytes, const void *frames,
                                     hcpp::u32 frame_count) noexcept {
        if (frames == nullptr || frame_bytes == 0) {
            return 0;
        }
        const hcpp::u32 avail = audio_ring_space(ring) / frame_bytes;
        const hcpp::u32 count = frame_count < avail ? frame_count : avail;
        audio_ring_push(ring, frames, count * frame_bytes);
        return count;
    }

    hcpp::u32 audio_ring_pop_frames(AudioRing &ring, hcpp::u32 frame_bytes, void *frames,
                                    hcpp::u32 frame_count) noexcept {
        if (frames == nullptr || frame_bytes == 0) {
            return 0;
        }
        const hcpp::u32 avail = audio_ring_used(ring) / frame_bytes;
        if (frame_count > avail) {
            ++ring.underruns;
        }
        const hcpp::u32 count = frame_count < avail ? frame_count : avail;
        audio_ring_pop(ring, frames, count * frame_bytes);
        return count;
    }

    hcpp::u32 audio_ring_used_frames(const AudioRing &ring, hcpp::u32 frame_bytes) noexcept {
        return frame_bytes == 0 ? 0 : audio_ring_used(ring) / frame_bytes;
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
        client.gain_q8 = audio_gain_unity_q8;
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

    bool audio_client_set_gain(hcpp::u32 client, hcpp::u16 gain_q8) noexcept {
        if (client == 0 || client > client_count || !clients[client - 1].active) {
            return false;
        }
        clients[client - 1].gain_q8 = gain_q8;
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
        hda::HdaController controller{};
        hcpp::u32 vendor_id = 0;
        if (!hda::hda_bind(card_index, controller) || !hda::hda_reset(controller) ||
            !hda::hda_codec_verb(controller, codec_address, 0, hda::verb_get_parameter,
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

    void mix_clear(hcpp::i16 *dst, hcpp::u32 frames) noexcept {
        if (dst == nullptr || frames == 0) {
            return;
        }
        hype_audio_buffer_clear(dst, frames);
    }

    void mix_add(hcpp::i16 *dst, const hcpp::i16 *src, hcpp::u32 frames,
                 hcpp::u16 gain_q8) noexcept {
        if (dst == nullptr || src == nullptr || frames == 0) {
            return;
        }
        hcpp::u32 i = 0;
#if defined(__SSE2__)
        const __m128i vgain = _mm_set1_epi16(static_cast<short>(gain_q8));
        if (gain_q8 == audio_gain_unity_q8) {
            for (; i + 8 <= frames; i += 8) {
                const __m128i vs =
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
                const __m128i vd =
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + i));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i),
                                 _mm_adds_epi16(vd, vs));
            }
        }
        for (; i + 8 <= frames; i += 8) {
            const __m128i vs =
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
            const __m128i vd =
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + i));
            const __m128i vsign = _mm_srai_epi16(vd, 15);
            const __m128i d0 = _mm_unpacklo_epi16(vd, vsign);
            const __m128i d1 = _mm_unpackhi_epi16(vd, vsign);
            const __m128i lo = _mm_mullo_epi16(vs, vgain);
            const __m128i hi = _mm_mulhi_epi16(vs, vgain);
            const __m128i p0 = _mm_srai_epi32(_mm_unpacklo_epi16(lo, hi), 8);
            const __m128i p1 = _mm_srai_epi32(_mm_unpackhi_epi16(lo, hi), 8);
            const __m128i r0 = _mm_add_epi32(d0, p0);
            const __m128i r1 = _mm_add_epi32(d1, p1);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i),
                             _mm_packs_epi32(r0, r1));
        }
#endif
        for (; i < frames; ++i) {
            dst[i] = fx_clip(static_cast<hcpp::i32>(dst[i]) +
                             ((static_cast<hcpp::i32>(src[i]) * gain_q8) >> 8));
        }
    }

    MixClipStats mix_clip_stats(const hcpp::i16 *samples, hcpp::u32 frames) noexcept {
        MixClipStats stats{0, 0};
        if (samples == nullptr) {
            return stats;
        }
        for (hcpp::u32 i = 0; i < frames; ++i) {
            const hcpp::i32 value = samples[i];
            const hcpp::u32 magnitude =
                value < 0 ? static_cast<hcpp::u32>(-value) : static_cast<hcpp::u32>(value);
            if (magnitude > stats.peak) {
                stats.peak = magnitude;
            }
            if (value >= 32767 || value <= -32768) {
                ++stats.clipped;
            }
        }
        return stats;
    }

    void pcm_mono_to_stereo(hcpp::i16 *dst, const hcpp::i16 *src, hcpp::u32 frames) noexcept {
        if (dst == nullptr || src == nullptr || frames == 0) {
            return;
        }
        hype_audio_pcm_mono_to_stereo_s16(dst, src, frames);
    }

    void pcm_swap_channels(hcpp::i16 *stereo, hcpp::u32 frames) noexcept {
        if (stereo == nullptr || frames == 0) {
            return;
        }
        hype_audio_pcm_swap_channels_s16(stereo, frames);
    }

    hcpp::u32 src_output_frames(hcpp::u32 in_frames, hcpp::u32 in_rate,
                                hcpp::u32 out_rate) noexcept {
        if (in_rate == 0 || out_rate == 0) {
            return 0;
        }
        return static_cast<hcpp::u32>(
            (static_cast<hcpp::u64>(in_frames) * out_rate + in_rate - 1) / in_rate);
    }

    hcpp::u32 src_convert(const hcpp::i16 *in, hcpp::u32 in_frames, hcpp::u32 in_rate,
                          hcpp::i16 *out, hcpp::u32 out_capacity, hcpp::u32 out_rate) noexcept {
        if (in == nullptr || out == nullptr || in_frames == 0 || in_rate == 0 ||
            out_rate == 0 || out_capacity == 0) {
            return 0;
        }
        if (in_rate == out_rate) {
            const hcpp::u32 count = in_frames < out_capacity ? in_frames : out_capacity;
            hype_audio_pcm_copy(out, in, count * sizeof(hcpp::i16));
            return count;
        }
        const hcpp::u32 wanted = src_output_frames(in_frames, in_rate, out_rate);
        const hcpp::u32 count = wanted < out_capacity ? wanted : out_capacity;
        const hcpp::u32 step_q16 = static_cast<hcpp::u32>(
            (static_cast<hcpp::u64>(in_rate) << 16) / out_rate);
        hcpp::u32 pos_q16 = 0;
        for (hcpp::u32 i = 0; i < count; ++i) {
            const hcpp::u32 index = pos_q16 >> 16;
            const hcpp::u32 frac = pos_q16 & 0xFFFF;
            const hcpp::i32 a = in[index];
            const hcpp::i32 b = index + 1 < in_frames ? in[index + 1] : in[index];
            out[i] = fx_clip(a + static_cast<hcpp::i32>(
                                     (static_cast<hcpp::i64>(b - a) * frac) >> 16));
            pos_q16 += step_q16;
        }
        return count;
    }

    bool src_selftest() noexcept {
        hcpp::i16 in[441];
        waveform_render_sine(in, 441, 220, 44100, 1);
        hcpp::i16 out[512];
        const hcpp::u32 expected = src_output_frames(441, 44100, 48000);
        const hcpp::u32 produced = src_convert(in, 441, 44100, out, 512, 48000);
        bool ok = expected == 480 && produced == 480 && out[0] == in[0];
        hcpp::i16 peak = 0;
        for (hcpp::u32 i = 0; i < 441; ++i) {
            if (in[i] > peak || -in[i] > peak) {
                peak = in[i] > 0 ? in[i] : static_cast<hcpp::i16>(-in[i]);
            }
        }
        for (hcpp::u32 i = 1; i < produced; ++i) {
            const hcpp::i32 step = static_cast<hcpp::i32>(out[i]) - out[i - 1];
            const hcpp::i32 bound = static_cast<hcpp::i32>(peak) / 16 + 64;
            if (step > bound || step < -bound || out[i] > peak + 16 ||
                out[i] < -peak - 16) {
                ok = false;
            }
        }
        serial::write(ok ? "src conv=ok\r\n" : "src conv=fail\r\n");
        return ok;
    }

    bool mix_selftest() noexcept {
        bool ok = true;
        bool mix_ok = true;
        hcpp::i16 dst[16];
        hcpp::i16 asm_dst[16];
        hcpp::i16 src[16];
        for (hcpp::u32 i = 0; i < 16; ++i) {
            dst[i] = 0;
            asm_dst[i] = 0;
            src[i] = 20000;
        }
        mix_clear(dst, 16);
        mix_add(dst, src, 16, audio_gain_unity_q8);
        mix_add(dst, src, 16, audio_gain_unity_q8);
        hype_audio_mix_s16(asm_dst, src, 16);
        hype_audio_mix_s16(asm_dst, src, 16);
        for (hcpp::u32 i = 0; i < 16; ++i) {
            if (dst[i] != 32767 || asm_dst[i] != 32767) {
                mix_ok = false;
            }
        }
        const MixClipStats stats = mix_clip_stats(dst, 16);
        hcpp::i16 gain_dst[8]{};
        hcpp::i16 gain_src[8];
        for (hcpp::u32 i = 0; i < 8; ++i) {
            gain_src[i] = 1000;
        }
        mix_add(gain_dst, gain_src, 8, 128);
        if (gain_dst[0] != 500 || gain_dst[7] != 500 || stats.clipped != 16 ||
            stats.peak != 32767) {
            mix_ok = false;
        }
        serial::write(mix_ok ? "mix sse=ok\r\n" : "mix sse=fail\r\n");
        ok = ok && mix_ok;
        bool fx_ok = true;
        hcpp::i16 fade[16];
        for (hcpp::u32 i = 0; i < 16; ++i) {
            fade[i] = 4096;
        }
        fx_fade(fade, 16, 0, fx_gain_unity_q8);
        fx_ok = fx_ok && fade[0] == 0 && fade[15] == 4096;
        for (hcpp::u32 i = 1; i < 16; ++i) {
            if (fade[i] < fade[i - 1]) {
                fx_ok = false;
            }
        }
        hcpp::i16 down[8];
        for (hcpp::u32 i = 0; i < 8; ++i) {
            down[i] = 4096;
        }
        fx_fade(down, 8, fx_gain_unity_q8, 0);
        fx_ok = fx_ok && down[0] == 4096 && down[7] == 0;
        for (hcpp::u32 i = 1; i < 8; ++i) {
            if (down[i] > down[i - 1]) {
                fx_ok = false;
            }
        }
        hcpp::i16 scaled[4] = {1000, -1000, 2000, -2000};
        fx_gain(scaled, 4, 128);
        fx_ok = fx_ok && scaled[0] == 500 && scaled[1] == -500 && scaled[2] == 1000 &&
                scaled[3] == -1000;
        hcpp::i16 panned[4] = {1000, 1000, 1000, 1000};
        fx_pan(panned, 2, fx_pan_right);
        fx_ok = fx_ok && panned[0] == 0 && panned[1] == 1000 && panned[2] == 0 &&
                panned[3] == 1000;
        serial::write(fx_ok ? "fx fade=ok\r\n" : "fx fade=fail\r\n");
        ok = ok && fx_ok;
        ok = src_selftest() && ok;
        bool ring_ok = true;
        AudioRing ring;
        audio_ring_init(ring);
        static hcpp::u8 push_a[6000];
        for (hcpp::u32 i = 0; i < 6000; ++i) {
            push_a[i] = static_cast<hcpp::u8>(i);
        }
        ring_ok = ring_ok && audio_ring_push(ring, push_a, 6000) == 6000;
        static hcpp::u8 popped[6000];
        ring_ok = ring_ok && audio_ring_pop(ring, popped, 4000) == 4000;
        for (hcpp::u32 i = 0; i < 4000; ++i) {
            if (popped[i] != push_a[i]) {
                ring_ok = false;
            }
        }
        static hcpp::u8 push_b[4000];
        for (hcpp::u32 i = 0; i < 4000; ++i) {
            push_b[i] = 0xA5;
        }
        ring_ok = ring_ok && audio_ring_push(ring, push_b, 4000) == 4000;
        ring_ok = ring_ok && ring.write_pos < ring.read_pos;
        static hcpp::u8 rest[6200];
        ring_ok = ring_ok && audio_ring_pop(ring, rest, 6200) == 6000;
        for (hcpp::u32 i = 0; i < 2000; ++i) {
            if (rest[i] != push_a[i + 4000]) {
                ring_ok = false;
            }
        }
        for (hcpp::u32 i = 2000; i < 6000; ++i) {
            if (rest[i] != 0xA5) {
                ring_ok = false;
            }
        }
        AudioRing frames_ring;
        audio_ring_init(frames_ring);
        hcpp::u32 frames_in[8];
        for (hcpp::u32 i = 0; i < 8; ++i) {
            frames_in[i] = 0x1000 + i;
        }
        hcpp::u32 frames_out[16]{};
        ring_ok = ring_ok &&
                  audio_ring_push_frames(frames_ring, 4, frames_in, 8) == 8 &&
                  audio_ring_used_frames(frames_ring, 4) == 8;
        ring_ok = ring_ok && audio_ring_pop_frames(frames_ring, 4, frames_out, 16) == 8 &&
                  audio_ring_underruns(frames_ring) == 1;
        for (hcpp::u32 i = 0; i < 8; ++i) {
            if (frames_out[i] != frames_in[i]) {
                ring_ok = false;
            }
        }
        serial::write(ring_ok ? "buf ring=ok\r\n" : "buf ring=fail\r\n");
        ok = ok && ring_ok;
        hcpp::i16 mono[4] = {7, 9, 11, 13};
        hcpp::i16 stereo[8]{};
        pcm_mono_to_stereo(stereo, mono, 4);
        hcpp::i16 pair[4] = {1, 2, 3, 4};
        pcm_swap_channels(pair, 2);
        ok = ok && stereo[0] == 7 && stereo[1] == 7 && stereo[4] == 11 && stereo[6] == 13 &&
             pair[0] == 2 && pair[1] == 1 && pair[2] == 4 && pair[3] == 3;
        return ok;
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
        ok = mix_selftest() && ok;
        return ok;
    }

}  // namespace hype::audio
