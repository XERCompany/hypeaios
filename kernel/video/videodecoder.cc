#include "kernel/video/videodecoder.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/codec.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/video/videodecoderlz.h"
#include "kernel/video/videodecoderpcie.h"
#include "kernel/video/videomcodec.h"
#include "kernel/video/videomp2.h"
#include "kernel/video/videoraw.h"
namespace hype::video {
    namespace {
        hcpp::u8 mjpeg_scratch[262144];
    }
    const char* codec_type_name(CodecType c) noexcept {
        switch (c) {
            case CodecType::Raw: return "raw";
            case CodecType::Lz: return "lzf";
            case CodecType::Mpeg2: return "mpeg2";
            case CodecType::Hardware: return "hw";
            case CodecType::Mjpeg: return "mjpeg";
            case CodecType::Ffv1: return "ffv1";
            case CodecType::H264: return "h264";
            default: return "unknown";
        }
    }
    bool mjpeg_probe(const hcpp::u8* src, hcpp::size_t n) noexcept {
        return n >= 4 && src[0] == 0xFF && src[1] == 0xD8 && src[2] == 0xFF;
    }
    DecodeResult mjpeg_decode_frame(const hcpp::u8* src, hcpp::size_t n, VideoFrame& out) noexcept {
        if (!mjpeg_probe(src, n) || out.data == nullptr)
            return {false, 0, 0, CodecType::Mjpeg};
        codec::Arena arena{mjpeg_scratch, sizeof(mjpeg_scratch), 0};
        codec::FrameBuf fb{};
        fb.data = out.data;
        fb.capacity = out.size;
        if (codec::image::jpeg_decode(src, n, &fb, arena) != codec::CodecStatus::Ok)
            return {false, n, 0, CodecType::Mjpeg};
        out.width = fb.width;
        out.height = fb.height;
        out.pitch = fb.pitch;
        out.format = PixelFormat::RGB32;
        out.size = fb.size;
        return {true, n, fb.size, CodecType::Mjpeg};
    }
    DecodeResult video_decode(CodecType c, const hcpp::u8* src, hcpp::size_t n, VideoFrame& out) noexcept {
        DecodeFn fn = mcodec_find(c);
        if (fn != nullptr) return fn(src, n, out);
        switch (c) {
            case CodecType::Raw: return raw_decode_frame(src, n, out);
            case CodecType::Lz: return lz_decode_frame(src, n, out);
            case CodecType::Mjpeg: return mjpeg_decode_frame(src, n, out);
            case CodecType::Ffv1: return ffv1_decode_frame(src, n, out);
            case CodecType::Hardware: return pcie_decode_frame(src, n, out);
            default: return {false, 0, 0, c};
        }
    }
    bool video_decode_frame(CodecType c, const hcpp::u8* p, hcpp::size_t n,
                            VideoFrame& out) noexcept {
        return video_decode(c, p, n, out).ok;
    }
    codec::CodecStatus video_mjpeg_decode(const hcpp::u8* p, hcpp::size_t n, void* out,
                                          codec::Arena& arena) noexcept {
        return codec::image::jpeg_decode(p, n, out, arena);
    }
    codec::CodecStatus video_mpeg2_decode(const hcpp::u8* p, hcpp::size_t n, void* out,
                                          codec::Arena&) noexcept {
        Mp2SeqInfo seq{};
        if (!mp2_seq_parse(p, n, seq)) return codec::CodecStatus::BadInput;
        codec::FrameBuf* fb = static_cast<codec::FrameBuf*>(out);
        if (fb != nullptr) {
            fb->width = seq.width;
            fb->height = seq.height;
        }
        return codec::CodecStatus::Unsupported;
    }
}  // namespace hype::video
