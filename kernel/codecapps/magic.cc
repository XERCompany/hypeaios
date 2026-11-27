#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/audio/adpcm.h"
#include "kernel/codecapps/audio/alac_codec.h"
#include "kernel/codecapps/audio/flac.h"
#include "kernel/codecapps/audio/mp3.h"
#include "kernel/codecapps/audio/ogg.h"
#include "kernel/codecapps/audio/vorbis.h"
#include "kernel/codecapps/audio/wav.h"
#include "kernel/codecapps/codec.h"
#include "kernel/codecapps/image/bmp.h"
#include "kernel/codecapps/image/exif.h"
#include "kernel/codecapps/image/gif.h"
#include "kernel/codecapps/image/ico.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/codecapps/image/png.h"
#include "kernel/codecapps/image/tiff.h"
#include "kernel/codecapps/image/pnm.h"
#include "kernel/codecapps/image/qoi.h"
#include "kernel/codecapps/image/tga.h"
#include "kernel/codecapps/registry.h"
#include "kernel/video/videodecoder.h"

namespace hype::codec {

    namespace {

        bool bytes_eq(const hcpp::u8 *buffer, hcpp::size_t size, hcpp::size_t offset,
                      const char *sig, hcpp::size_t sig_size) noexcept {
            if (buffer == nullptr || offset > size || sig_size > size - offset) return false;
            for (hcpp::size_t i = 0; i < sig_size; ++i)
                if (buffer[offset + i] != static_cast<hcpp::u8>(sig[i])) return false;
            return true;
        }

        bool str_eq_at(const hcpp::u8 *buffer, hcpp::size_t size, hcpp::size_t offset,
                       const char *text) noexcept {
            hcpp::size_t len = 0;
            while (text[len] != '\0') ++len;
            return bytes_eq(buffer, size, offset, text, len);
        }

        bool brand_in(const hcpp::u8 *buffer, hcpp::size_t size, const char *a, const char *b,
                      const char *c, const char *d) noexcept {
            const char *brands[] = {a, b, c, d};
            for (hcpp::size_t i = 0; i < 4; ++i)
                if (brands[i] != nullptr && str_eq_at(buffer, size, 8, brands[i])) return true;
            return false;
        }

        bool probe_bmp(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return n >= 2 && b[0] == 'B' && b[1] == 'M';
        }

        bool probe_pnm(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return n >= 2 && b[0] == 'P' && b[1] >= '1' && b[1] <= '6';
        }

        bool probe_tga(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return image::tga_probe(b, n);
        }

        bool probe_qoi(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "qoif");
        }

        bool probe_png(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);
        }

        bool probe_gif(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "GIF8");
        }

        bool probe_jpeg(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "\xFF\xD8\xFF", 3);
        }

        bool probe_ico(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return n >= 4 && b[0] == 0 && b[1] == 0 && b[3] == 0 && (b[2] == 1 || b[2] == 2);
        }

        bool probe_tiff(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "II*\x00", 4) || bytes_eq(b, n, 0, "MM\x00*", 4);
        }

        bool probe_psd(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "8BPS");
        }

        bool probe_webp(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "RIFF") && str_eq_at(b, n, 8, "WEBP");
        }

        bool probe_jpeg2000(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (bytes_eq(b, n, 0, "\xFF\x4F\xFF\x51", 4)) return true;
            return bytes_eq(b, n, 0, "\x00\x00\x00\x0CjP  ", 8);
        }

        bool probe_avif(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 4, "ftyp") && brand_in(b, n, "avif", "avis", nullptr, nullptr);
        }

        bool probe_heic(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 4, "ftyp") &&
                   brand_in(b, n, "heic", "heix", "hevc", "mif1");
        }

        bool probe_alac(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (!str_eq_at(b, n, 4, "ftyp")) return false;
            if (brand_in(b, n, "alac", "M4A ", nullptr, nullptr)) return true;
            if (brand_in(b, n, "isom", "iso2", "mp41", "mp42") ||
                str_eq_at(b, n, 8, "mp4 "))
                return false;
            hcpp::size_t scan = n;
            if (n >= 4) {
                const hcpp::u32 box = (static_cast<hcpp::u32>(b[0]) << 24) |
                                      (static_cast<hcpp::u32>(b[1]) << 16) |
                                      (static_cast<hcpp::u32>(b[2]) << 8) |
                                      static_cast<hcpp::u32>(b[3]);
                if (box >= 16 && box < scan) scan = box;
            }
            if (scan > 96) scan = 96;
            for (hcpp::size_t i = 16; i + 4 <= scan; ++i)
                if (b[i] == 'a' && b[i + 1] == 'l' && b[i + 2] == 'a' && b[i + 3] == 'c')
                    return true;
            return false;
        }

        bool probe_exif(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "Exif\x00\x00", 6);
        }

        hcpp::u16 wav_format_tag(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (!str_eq_at(b, n, 12, "fmt ") || n < 22) return 0;
            return static_cast<hcpp::u16>(b[20] | (static_cast<hcpp::u16>(b[21]) << 8));
        }

        bool probe_adpcm(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (!str_eq_at(b, n, 0, "RIFF") || !str_eq_at(b, n, 8, "WAVE")) return false;
            const hcpp::u16 tag = wav_format_tag(b, n);
            return tag == 0x11 || tag == 0x06 || tag == 0x07;
        }

        bool probe_wav(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "RIFF") && str_eq_at(b, n, 8, "WAVE");
        }

        bool probe_flac(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (str_eq_at(b, n, 0, "fLaC")) return true;
            return str_eq_at(b, n, 0, "OggS") && n >= 33 && b[28] == 0x7F &&
                   str_eq_at(b, n, 29, "FLAC");
        }

        bool probe_mp3(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (str_eq_at(b, n, 0, "ID3")) return true;
            return n >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0 && (b[1] & 0x06) == 0x02;
        }

        bool probe_mp2(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (n < 4 || b[0] != 0xFF || (b[1] & 0xE0) != 0xE0) return false;
            const hcpp::u8 version = static_cast<hcpp::u8>((b[1] >> 3) & 0x03);
            const hcpp::u8 layer = static_cast<hcpp::u8>((b[1] >> 1) & 0x03);
            if (version == 0x01 || layer != 0x02) return false;
            const hcpp::u8 bitrate = static_cast<hcpp::u8>((b[2] >> 4) & 0x0F);
            const hcpp::u8 sample_rate = static_cast<hcpp::u8>((b[2] >> 2) & 0x03);
            return bitrate != 0 && bitrate != 0x0F && sample_rate != 0x03;
        }

        bool probe_ac3(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (n < 6 || b[0] != 0x0B || b[1] != 0x77) return false;
            const hcpp::u8 fscod = static_cast<hcpp::u8>(b[4] >> 6);
            const hcpp::u8 frmsizecod = static_cast<hcpp::u8>(b[4] & 0x3F);
            const hcpp::u8 bsid = static_cast<hcpp::u8>(b[5] >> 3);
            return fscod != 0x03 && frmsizecod <= 37 && bsid <= 10;
        }

        bool probe_aac(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return n >= 2 && b[0] == 0xFF && (b[1] & 0xF6) == 0xF0;
        }

        bool probe_vorbis(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "OggS") && n >= 35 && b[28] == 0x01 &&
                   str_eq_at(b, n, 29, "vorbis");
        }

        bool probe_opus(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "OggS") && str_eq_at(b, n, 28, "OpusHead");
        }

        bool probe_ogg(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "OggS");
        }

        bool probe_mp4(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 4, "ftyp");
        }

        bool probe_mjpeg(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "\xFF\xD8\xFF", 3);
        }

        bool probe_mpeg2v(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            const hcpp::size_t scan = n < 256 ? n : 256;
            for (hcpp::size_t i = 0; i + 3 < scan; ++i)
                if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 && b[i + 3] == 0xB3)
                    return true;
            return false;
        }

        bool probe_h264(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (n >= 5 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1 &&
                (b[4] & 0x1F) == 7)
                return true;
            return n >= 5 && b[0] == 0 && b[1] == 0 && b[2] == 1 && (b[3] & 0x1F) == 7;
        }

        bool probe_avi(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return str_eq_at(b, n, 0, "RIFF") && str_eq_at(b, n, 8, "AVI ");
        }

        bool probe_ts(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            if (n < 188 || b[0] != 0x47) return false;
            if (n >= 376 && b[188] != 0x47) return false;
            if (n >= 564 && b[376] != 0x47) return false;
            return true;
        }

        bool probe_mkv(const hcpp::u8 *b, hcpp::size_t n) noexcept {
            return bytes_eq(b, n, 0, "\x1A\x45\xDF\xA3", 4);
        }

        const CodecDescriptor builtin_descriptors[] = {
            {"bmp", CodecKind::Image, CodecId::Bmp,
             codec_flag_decode | codec_flag_encode, probe_bmp, image::bmp_decode,
             image::bmp_encode},
            {"pnm", CodecKind::Image, CodecId::Pnm,
             codec_flag_decode | codec_flag_encode, probe_pnm, image::pnm_decode,
             image::pnm_encode},
            {"tga", CodecKind::Image, CodecId::Tga,
             codec_flag_decode | codec_flag_encode, probe_tga, image::tga_decode,
             image::tga_encode},
            {"qoi", CodecKind::Image, CodecId::Qoi,
             codec_flag_decode | codec_flag_encode, probe_qoi, image::qoi_decode,
             image::qoi_encode},
            {"png", CodecKind::Image, CodecId::Png,
             codec_flag_decode | codec_flag_encode, probe_png, image::png_decode,
             image::png_encode},
            {"gif", CodecKind::Image, CodecId::Gif, codec_flag_decode, probe_gif,
             image::gif_decode, codec_stub_encode},
            {"jpeg", CodecKind::Image, CodecId::Jpeg, codec_flag_decode, probe_jpeg,
             image::jpeg_decode, codec_stub_encode},
            {"ico", CodecKind::Image, CodecId::Ico, codec_flag_decode, probe_ico,
             image::ico_decode, codec_stub_encode},
            {"tiff", CodecKind::Image, CodecId::Tiff, codec_flag_decode, probe_tiff,
             image::tiff_decode, codec_stub_encode},
            {"psd", CodecKind::Image, CodecId::Psd, 0, probe_psd, codec_stub_decode,
             codec_stub_encode},
            {"webp", CodecKind::Image, CodecId::WebP, 0, probe_webp, codec_stub_decode,
             codec_stub_encode},
            {"jp2", CodecKind::Image, CodecId::Jpeg2000, 0, probe_jpeg2000, codec_stub_decode,
             codec_stub_encode},
            {"avif", CodecKind::Image, CodecId::Avif, 0, probe_avif, codec_stub_decode,
             codec_stub_encode},
            {"heic", CodecKind::Image, CodecId::Heic, 0, probe_heic, codec_stub_decode,
             codec_stub_encode},
            {"alac", CodecKind::Audio, CodecId::Alac,
             codec_flag_decode | codec_flag_encode, probe_alac, audio_alac_decode,
             audio_alac_encode},
            {"exif", CodecKind::Image, CodecId::Exif, codec_flag_decode, probe_exif,
             image::exif_decode, codec_stub_encode},
            {"adpcm", CodecKind::Audio, CodecId::Adpcm, codec_flag_decode, probe_adpcm,
             audio_adpcm_decode, codec_stub_encode},
            {"wav", CodecKind::Audio, CodecId::Wav, codec_flag_decode, probe_wav,
             audio_wav_decode, codec_stub_encode},
            {"vorbis", CodecKind::Audio, CodecId::Vorbis, codec_flag_decode, probe_vorbis,
             audio_vorbis_decode, codec_stub_encode},
            {"opus", CodecKind::Audio, CodecId::Opus, 0, probe_opus, codec_stub_decode,
             codec_stub_encode},
            {"flac", CodecKind::Audio, CodecId::Flac, codec_flag_decode, probe_flac,
             audio_flac_decode, codec_stub_encode},
            {"mp3", CodecKind::Audio, CodecId::Mp3, codec_flag_decode, probe_mp3,
             audio_mp3_decode, codec_stub_encode},
            {"mp2", CodecKind::Audio, CodecId::Mp2, 0, probe_mp2, codec_stub_decode,
             codec_stub_encode},
            {"ac3", CodecKind::Audio, CodecId::Ac3, 0, probe_ac3, codec_stub_decode,
             codec_stub_encode},
            {"aac", CodecKind::Audio, CodecId::Aac, 0, probe_aac, codec_stub_decode,
             codec_stub_encode},
            {"ogg", CodecKind::Container, CodecId::Ogg, codec_flag_decode, probe_ogg,
             audio_ogg_decode, codec_stub_encode},
            {"mp4", CodecKind::Container, CodecId::Mp4, 0, probe_mp4, codec_stub_decode,
             codec_stub_encode},
            {"mjpeg", CodecKind::Video, CodecId::Mjpeg, codec_flag_decode, probe_mjpeg,
             video::video_mjpeg_decode, codec_stub_encode},
            {"mpeg2", CodecKind::Video, CodecId::Mpeg2, 0, probe_mpeg2v,
             video::video_mpeg2_decode, codec_stub_encode},
            {"h264", CodecKind::Video, CodecId::H264, 0, probe_h264, codec_stub_decode,
             codec_stub_encode},
            {"avi", CodecKind::Container, CodecId::Avi, 0, probe_avi, codec_stub_decode,
             codec_stub_encode},
            {"ts", CodecKind::Container, CodecId::Ts, 0, probe_ts, codec_stub_decode,
             codec_stub_encode},
            {"mkv", CodecKind::Container, CodecId::Mkv, 0, probe_mkv, codec_stub_decode,
             codec_stub_encode},
        };

        struct ProbeCase {
            CodecId id;
            hcpp::u8 bytes[36];
            hcpp::size_t size;
        };

        const ProbeCase probe_cases[] = {
            {CodecId::Bmp, {'B', 'M', 0x36, 0x00}, 4},
            {CodecId::Pnm, {'P', '6', ' ', '4'}, 4},
            {CodecId::Tga,
             {'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N', '-', 'X', 'F', 'I', 'L', 'E',
              '.', 0x00},
             18},
            {CodecId::Qoi, {'q', 'o', 'i', 'f'}, 4},
            {CodecId::Png, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 8},
            {CodecId::Gif, {'G', 'I', 'F', '8', '9', 'a'}, 6},
            {CodecId::Jpeg, {0xFF, 0xD8, 0xFF, 0xE0}, 4},
            {CodecId::Ico, {0x00, 0x00, 0x01, 0x00}, 4},
            {CodecId::Tiff, {'I', 'I', 0x2A, 0x00}, 4},
            {CodecId::Psd, {'8', 'B', 'P', 'S'}, 4},
            {CodecId::WebP, {'R', 'I', 'F', 'F', 0x24, 0x00, 0x00, 0x00, 'W', 'E', 'B', 'P'}, 12},
            {CodecId::Jpeg2000, {0xFF, 0x4F, 0xFF, 0x51}, 4},
            {CodecId::Avif,
             {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f'},
             12},
            {CodecId::Heic,
             {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c'},
             12},
            {CodecId::Exif, {'E', 'x', 'i', 'f', 0x00, 0x00}, 6},
            {CodecId::Adpcm,
             {'R', 'I', 'F', 'F', 0x24, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
              0x10, 0x00, 0x00, 0x00, 0x11, 0x00},
             22},
            {CodecId::Wav,
             {'R', 'I', 'F', 'F', 0x24, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
              0x10, 0x00, 0x00, 0x00, 0x01, 0x00},
             22},
            {CodecId::Flac, {'f', 'L', 'a', 'C'}, 4},
            {CodecId::Mp3, {0xFF, 0xFB, 0x90, 0x00}, 4},
            {CodecId::Mp3, {'I', 'D', '3', 0x04}, 4},
            {CodecId::Mp2, {0xFF, 0xFD, 0x90, 0x00}, 4},
            {CodecId::Ac3, {0x0B, 0x77, 0x00, 0x00, 0x45, 0x40}, 6},
            {CodecId::Aac, {0xFF, 0xF1, 0x50, 0x80}, 4},
            {CodecId::Vorbis,
             {'O', 'g', 'g', 'S', 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x01, 'v', 'o', 'r', 'b', 'i', 's'},
             35},
            {CodecId::Opus,
             {'O', 'g', 'g', 'S', 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'},
             36},
            {CodecId::Ogg, {'O', 'g', 'g', 'S'}, 4},
            {CodecId::Mp4,
             {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'},
             12},
            {CodecId::Mpeg2,
             {0x00, 0x00, 0x01, 0xB3, 0x0A, 0x00, 0x78, 0x23, 0xFF, 0xFF, 0xFF, 0xF8},
             12},
            {CodecId::H264, {0x00, 0x00, 0x00, 0x01, 0x67}, 5},
            {CodecId::Avi,
             {'R', 'I', 'F', 'F', 0x10, 0x00, 0x00, 0x00, 'A', 'V', 'I', ' '},
             12},
            {CodecId::Mkv, {0x1A, 0x45, 0xDF, 0xA3}, 4},
        };

        const hcpp::u8 unknown_blob[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    }  // namespace

    void codec_register_builtins() noexcept {
        for (hcpp::size_t i = 0; i < sizeof(builtin_descriptors) / sizeof(builtin_descriptors[0]);
             ++i)
            codec_register(builtin_descriptors[i]);
    }

    bool codec_probe_selftest() noexcept {
        for (hcpp::size_t i = 0; i < sizeof(probe_cases) / sizeof(probe_cases[0]); ++i)
            if (codec_probe(probe_cases[i].bytes, probe_cases[i].size) != probe_cases[i].id)
                return false;
        return codec_probe(unknown_blob, sizeof(unknown_blob)) == CodecId::Unknown &&
               codec_probe(nullptr, 0) == CodecId::Unknown;
    }

    bool codec_arena_selftest() noexcept {
        static hcpp::u8 backing[256];
        Arena arena{backing, sizeof(backing), 0};
        void *first = arena_alloc(arena, 17, 8);
        if (first != backing) return false;
        void *second = arena_alloc(arena, 8, 16);
        if (second != backing + 32) return false;
        if (arena_alloc(arena, 4096, 8) != nullptr) return false;
        if (arena_alloc(arena, 8, 3) != nullptr) return false;
        arena_reset(arena);
        return arena.used == 0 && arena_alloc(arena, 256, 16) == backing;
    }

}  // namespace hype::codec
