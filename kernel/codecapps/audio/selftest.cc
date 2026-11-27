#include "kernel/codecapps/audio/selftest.h"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/audio/clips.h"
#include "kernel/codecapps/audio/ogg.h"
#include "kernel/codecapps/registry.h"
#include "kernel/serial.hpp"

namespace hype::codec {

    namespace {

        constexpr hcpp::size_t pcm_capacity = 16384;
        constexpr hcpp::size_t ogg_payload_capacity = 4096;
        constexpr hcpp::size_t ogg_packet_capacity = 32;
        constexpr hcpp::size_t vorbis_arena_pages = 96;

        void mark(const char *name, bool ok) noexcept {
            serial::write("audio decode=");
            serial::write(name);
            serial::write(" ");
            serial::write_line(ok ? "ok" : "fail");
        }

        void mark_named(const char *name, bool ok) noexcept {
            serial::write(name);
            serial::write("=");
            serial::write_line(ok ? "ok" : "fail");
        }

        bool pcm_eq(const PcmBuf &buf, const hcpp::i16 *expected,
                    hcpp::size_t frames) noexcept {
            if (buf.frames != frames || expected == nullptr) return false;
            const auto *got = reinterpret_cast<const hcpp::i16 *>(buf.data);
            for (hcpp::size_t i = 0; i < frames * buf.format.channels; ++i)
                if (got[i] != expected[i]) return false;
            return true;
        }

        bool run_clip(CodecId id, const hcpp::u8 *clip, hcpp::size_t clip_size,
                      const hcpp::i16 *expected, hcpp::size_t frames, hcpp::u8 channels,
                      Arena &arena) noexcept {
            if (codec_probe(clip, clip_size) != id) return false;
            PcmBuf buf{};
            buf.data = static_cast<hcpp::u8 *>(arena_alloc(arena, pcm_capacity, 8));
            buf.capacity = pcm_capacity;
            if (buf.data == nullptr) return false;
            if (codec_decode(id, clip, clip_size, &buf, arena) != CodecStatus::Ok)
                return false;
            if (buf.format.channels != channels || buf.format.sample_rate != 8000 ||
                buf.format.sample_format != audio::AudioSampleFormat::S16Le)
                return false;
            if (expected != nullptr) return pcm_eq(buf, expected, frames);
            if (buf.frames == 0) return false;
            const auto *pcm = reinterpret_cast<const hcpp::i16 *>(buf.data);
            for (hcpp::size_t i = 0; i < buf.frames * channels; ++i)
                if (pcm[i] != 0) return true;
            return false;
        }

        bool ogg_demux_selftest(Arena &arena) noexcept {
            if (codec_probe(clip_ogg, sizeof(clip_ogg)) != CodecId::Ogg) return false;
            OggPackets packets{};
            packets.payload = static_cast<hcpp::u8 *>(
                arena_alloc(arena, ogg_payload_capacity, 8));
            packets.offsets = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ogg_packet_capacity * 4, 8));
            packets.sizes = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ogg_packet_capacity * 4, 8));
            packets.payload_cap = ogg_payload_capacity;
            packets.packet_cap = ogg_packet_capacity;
            if (packets.payload == nullptr || packets.offsets == nullptr ||
                packets.sizes == nullptr)
                return false;
            if (codec_decode(CodecId::Ogg, clip_ogg, sizeof(clip_ogg), &packets,
                             arena) != CodecStatus::Ok)
                return false;
            constexpr hcpp::size_t want =
                sizeof(clip_ogg_sizes) / sizeof(clip_ogg_sizes[0]);
            if (packets.packet_count != want || packets.truncated ||
                packets.payload_used != sizeof(clip_ogg_payload))
                return false;
            for (hcpp::size_t i = 0; i < want; ++i)
                if (packets.sizes[i] != clip_ogg_sizes[i]) return false;
            for (hcpp::size_t i = 0; i < packets.payload_used; ++i)
                if (packets.payload[i] != clip_ogg_payload[i]) return false;
            return true;
        }

        bool vorbis_decode_selftest() noexcept {
            if (codec_probe(clip_vorbis, sizeof(clip_vorbis)) != CodecId::Vorbis)
                return false;
            Arena arena{};
            if (!arena_from_pmm(arena, vorbis_arena_pages)) return false;
            PcmBuf buf{};
            buf.data = static_cast<hcpp::u8 *>(arena_alloc(arena, pcm_capacity, 8));
            buf.capacity = pcm_capacity;
            bool pass = false;
            if (buf.data != nullptr &&
                codec_decode(CodecId::Vorbis, clip_vorbis, sizeof(clip_vorbis),
                             &buf, arena) == CodecStatus::Ok &&
                buf.format.channels == 1 && buf.format.sample_rate == 8000 &&
                buf.format.sample_format == audio::AudioSampleFormat::S16Le &&
                buf.frames != 0)
                pass = true;
            arena_release_pmm(arena, vorbis_arena_pages);
            return pass;
        }

        bool alac_roundtrip_selftest(Arena &arena, bool &enc_ok) noexcept {
            static const hcpp::i16 pcm_in[] = {0,     0,     1000,  -1000, 2000,  -2000,
                                               32767, -32768, 12345, -23456, -1,    1,
                                               4000,  -4000, 777,   -777};
            enc_ok = false;
            PcmBuf src{};
            src.data = const_cast<hcpp::u8 *>(reinterpret_cast<const hcpp::u8 *>(pcm_in));
            src.capacity = sizeof(pcm_in);
            src.frames = 8;
            src.format.channels = 2;
            src.format.sample_rate = 44100;
            src.format.sample_format = audio::AudioSampleFormat::S16Le;
            static hcpp::u8 encoded[8192];
            hcpp::size_t written = 0;
            if (codec_encode(CodecId::Alac, &src, encoded, sizeof(encoded), written,
                             arena) != CodecStatus::Ok ||
                written <= 40)
                return false;
            enc_ok = true;
            PcmBuf out{};
            out.data = static_cast<hcpp::u8 *>(arena_alloc(arena, pcm_capacity, 8));
            out.capacity = pcm_capacity;
            if (out.data == nullptr) return false;
            if (codec_decode(CodecId::Alac, encoded, written, &out, arena) != CodecStatus::Ok)
                return false;
            return pcm_eq(out, pcm_in, 8);
        }

        bool probe_stub_selftest() noexcept {
            static const hcpp::u8 mp2_header[] = {0xFF, 0xFD, 0x90, 0x00};
            static const hcpp::u8 ac3_header[] = {0x0B, 0x77, 0x00, 0x00, 0x45, 0x40};
            static const hcpp::u8 aac_header[] = {0xFF, 0xF1, 0x50, 0x80};
            return codec_probe(mp2_header, sizeof(mp2_header)) == CodecId::Mp2 &&
                   codec_probe(ac3_header, sizeof(ac3_header)) == CodecId::Ac3 &&
                   codec_probe(aac_header, sizeof(aac_header)) == CodecId::Aac;
        }

        bool vorbis_sniff_selftest(Arena &arena) noexcept {
            OggPackets packets{};
            packets.payload = static_cast<hcpp::u8 *>(
                arena_alloc(arena, ogg_payload_capacity, 8));
            packets.offsets = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ogg_packet_capacity * 4, 8));
            packets.sizes = static_cast<hcpp::u32 *>(
                arena_alloc(arena, ogg_packet_capacity * 4, 8));
            packets.payload_cap = ogg_payload_capacity;
            packets.packet_cap = ogg_packet_capacity;
            if (packets.payload == nullptr || packets.offsets == nullptr ||
                packets.sizes == nullptr)
                return false;
            if (codec_decode(CodecId::Ogg, clip_vorbis, sizeof(clip_vorbis),
                             &packets, arena) != CodecStatus::Ok)
                return false;
            if (packets.packet_count < 3) return false;
            return ogg_sniff_vorbis(packets.payload + packets.offsets[0],
                                    packets.sizes[0]);
        }

    }  // namespace

    bool audio_codec_selftest() noexcept {
        static hcpp::u8 backing[32768];
        Arena arena{backing, sizeof(backing), 0};
        bool ok = true;
        bool pass = run_clip(CodecId::Adpcm, clip_adpcm_wav, sizeof(clip_adpcm_wav),
                             clip_adpcm_pcm,
                             sizeof(clip_adpcm_pcm) / sizeof(clip_adpcm_pcm[0]), 1, arena);
        mark("adpcm", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = run_clip(CodecId::Adpcm, clip_g711_wav, sizeof(clip_g711_wav),
                        clip_g711_pcm, sizeof(clip_g711_pcm) / sizeof(clip_g711_pcm[0]),
                        1, arena);
        mark("g711", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = run_clip(CodecId::Mp3, clip_mp3, sizeof(clip_mp3), nullptr, 0, 1, arena);
        mark("mp3", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = run_clip(CodecId::Flac, clip_flac, sizeof(clip_flac), clip_flac_pcm,
                        sizeof(clip_flac_pcm) / sizeof(clip_flac_pcm[0]), 1, arena);
        mark("flac", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = ogg_demux_selftest(arena);
        mark_named("ogg demux", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = vorbis_sniff_selftest(arena) && vorbis_decode_selftest();
        mark_named("vorbis decode", pass);
        ok = ok && pass;
        arena_reset(arena);
        pass = run_clip(CodecId::Alac, clip_alac, sizeof(clip_alac), clip_alac_pcm,
                        sizeof(clip_alac_pcm) / sizeof(clip_alac_pcm[0]) / 2, 2, arena);
        mark_named("alac decode", pass);
        ok = ok && pass;
        arena_reset(arena);
        bool enc_ok = false;
        pass = alac_roundtrip_selftest(arena, enc_ok);
        mark_named("alac enc", enc_ok);
        mark_named("alac rt", pass);
        ok = ok && pass && enc_ok;
        arena_reset(arena);
        pass = probe_stub_selftest();
        mark_named("probe mp2/ac3/aac", pass);
        ok = ok && pass;
        return ok;
    }

}  // namespace hype::codec
