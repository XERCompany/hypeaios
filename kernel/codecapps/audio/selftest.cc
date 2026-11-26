#include "kernel/codecapps/audio/selftest.h"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/audio/clips.h"
#include "kernel/codecapps/registry.h"
#include "kernel/serial.hpp"

namespace hype::codec {

    namespace {

        constexpr hcpp::size_t pcm_capacity = 16384;

        void mark(const char *name, bool ok) noexcept {
            serial::write("audio decode=");
            serial::write(name);
            serial::write(" ");
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
        return ok;
    }

}  // namespace hype::codec
