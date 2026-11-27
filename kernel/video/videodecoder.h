#pragma once
#include "kernel/video/videoframe.h"
namespace hype::video {
    enum class CodecType : hcpp::u8 { Unknown, Raw, Lz, Mpeg2, Hardware, Mjpeg, Ffv1 };
    struct DecodeResult {
        bool ok;
        hcpp::size_t consumed, produced;
        CodecType codec;
    };
    using DecodeFn = DecodeResult (*)(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
    const char* codec_type_name(CodecType) noexcept;
    bool mjpeg_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    DecodeResult mjpeg_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
    DecodeResult video_decode(CodecType, const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
}  // namespace hype::video
