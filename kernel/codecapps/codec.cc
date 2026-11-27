#include "kernel/codecapps/codec.h"
#include "kernel/codecapps/arena.h"

namespace hype::codec {

    namespace {
        const char *id_names[] = {
            "unknown", "bmp", "pnm",  "tga",    "qoi",  "png",  "gif", "jpeg", "ico",
            "tiff",    "psd", "webp", "jp2",    "avif", "heic", "exif", "yuv",
            "wav",     "adpcm", "flac", "mp3",  "vorbis", "opus", "alac", "aac",
            "ogg",     "mp4", "mp2", "ac3",
        };
    }  // namespace

    const char *codec_kind_name(CodecKind kind) noexcept {
        switch (kind) {
            case CodecKind::Image:
                return "image";
            case CodecKind::Audio:
                return "audio";
            case CodecKind::Container:
                return "container";
        }
        return "unknown";
    }

    const char *codec_id_name(CodecId id) noexcept {
        const hcpp::size_t index = static_cast<hcpp::size_t>(id);
        if (index >= sizeof(id_names) / sizeof(id_names[0])) return "unknown";
        return id_names[index];
    }

    CodecStatus codec_stub_decode(const hcpp::u8 *, hcpp::size_t, void *, Arena &) noexcept {
        return CodecStatus::Unsupported;
    }

    CodecStatus codec_stub_encode(const void *, hcpp::u8 *, hcpp::size_t, hcpp::size_t &written,
                                  Arena &) noexcept {
        written = 0;
        return CodecStatus::Unsupported;
    }

}  // namespace hype::codec
