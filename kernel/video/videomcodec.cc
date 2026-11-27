#include "kernel/video/videomcodec.h"

#include "kernel/video/videodecoderlz.h"
#include "kernel/video/videodecoderpcie.h"
#include "kernel/video/videoraw.h"
namespace hype::video {
    namespace {
        struct Entry {
            CodecType codec;
            DecodeFn fn;
        };
        Entry entries[mcodec_max_entries];
        hcpp::size_t entry_count;
        bool builtins_done;

        DecodeResult stub_mpeg2_decode(const hcpp::u8* p, hcpp::size_t n,
                                       VideoFrame&) noexcept {
            return {false, p != nullptr ? n : 0, 0, CodecType::Mpeg2};
        }
        DecodeResult stub_h264_decode(const hcpp::u8* p, hcpp::size_t n,
                                      VideoFrame&) noexcept {
            return {false, p != nullptr ? n : 0, 0, CodecType::H264};
        }
    }  // namespace
    bool mcodec_register(CodecType c, DecodeFn f) noexcept {
        if (!f || entry_count >= mcodec_max_entries) return false;
        entries[entry_count++] = {c, f};
        return true;
    }
    DecodeFn mcodec_find(CodecType c) noexcept {
        if (!builtins_done) mcodec_register_builtins();
        for (hcpp::size_t i = 0; i < entry_count; ++i)
            if (entries[i].codec == c) return entries[i].fn;
        return nullptr;
    }
    DecodeResult mcodec_decode(CodecType c, const hcpp::u8* p, hcpp::size_t n,
                               VideoFrame& out) noexcept {
        DecodeFn fn = mcodec_find(c);
        if (fn == nullptr) return {false, 0, 0, c};
        return fn(p, n, out);
    }
    hcpp::size_t mcodec_count() noexcept {
        if (!builtins_done) mcodec_register_builtins();
        return entry_count;
    }
    hcpp::size_t mcodec_register_builtins() noexcept {
        if (builtins_done) return entry_count;
        builtins_done = true;
        mcodec_register(CodecType::Raw, raw_decode_frame);
        mcodec_register(CodecType::Lz, lz_decode_frame);
        mcodec_register(CodecType::Mjpeg, mjpeg_decode_frame);
        mcodec_register(CodecType::Ffv1, ffv1_decode_frame);
        mcodec_register(CodecType::Mpeg2, stub_mpeg2_decode);
        mcodec_register(CodecType::H264, stub_h264_decode);
        mcodec_register(CodecType::Hardware, pcie_decode_frame);
        return entry_count;
    }
}  // namespace hype::video
