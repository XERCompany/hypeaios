#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audioapi.h"
#include "kernel/video/videoframe.h"

namespace hype::codec {

    enum class CodecKind : hcpp::u8 { Image, Audio, Container };

    enum class CodecId : hcpp::u8 {
        Unknown,
        Bmp,
        Pnm,
        Tga,
        Qoi,
        Png,
        Gif,
        Jpeg,
        Ico,
        Tiff,
        Psd,
        WebP,
        Jpeg2000,
        Avif,
        Heic,
        Exif,
        RawYuv,
        Wav,
        Adpcm,
        Flac,
        Mp3,
        Vorbis,
        Opus,
        Alac,
        Aac,
        Ogg,
        Mp4,
        Mp2,
        Ac3,
        Count
    };

    enum class CodecStatus : hcpp::u8 { Ok, Unsupported, BadInput, NoSpace };

    constexpr hcpp::u8 codec_flag_decode = 1;
    constexpr hcpp::u8 codec_flag_encode = 2;
    constexpr hcpp::size_t codec_reserved_ids = 1;

    struct FrameBuf {
        hcpp::u32 width;
        hcpp::u32 height;
        hcpp::u32 pitch;
        video::PixelFormat format;
        hcpp::u8 *data;
        hcpp::size_t capacity;
        hcpp::size_t size;
    };

    struct PcmBuf {
        audio::AudioFormat format;
        hcpp::u8 *data;
        hcpp::size_t capacity;
        hcpp::size_t frames;
    };

    struct Arena;

    using CodecProbeFn = bool (*)(const hcpp::u8 *, hcpp::size_t) noexcept;
    using CodecDecodeFn = CodecStatus (*)(const hcpp::u8 *, hcpp::size_t, void *, Arena &) noexcept;
    using CodecEncodeFn = CodecStatus (*)(const void *, hcpp::u8 *, hcpp::size_t, hcpp::size_t &,
                                          Arena &) noexcept;

    struct CodecDescriptor {
        const char *name;
        CodecKind kind;
        CodecId id;
        hcpp::u8 flags;
        CodecProbeFn probe;
        CodecDecodeFn decode;
        CodecEncodeFn encode;
    };

    const char *codec_kind_name(CodecKind kind) noexcept;
    const char *codec_id_name(CodecId id) noexcept;
    CodecStatus codec_stub_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                                  Arena &arena) noexcept;
    CodecStatus codec_stub_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                                  hcpp::size_t &written, Arena &arena) noexcept;

}  // namespace hype::codec
