#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    enum class DecodeJobStatus : hcpp::u8 { Free, Queued, Running, Done, Failed, Unsupported };
    constexpr hcpp::u32 decodejob_flag_hw = 1;
    constexpr hcpp::size_t decodejob_queue_depth = 8;
    struct DecodeJob {
        CodecType codec_id;
        const hcpp::u8* in_buf;
        hcpp::size_t in_len;
        VideoFrame* out_frame;
        hcpp::u32 flags;
        DecodeJobStatus status;
        DecodeResult result;
    };
    void decodejob_reset() noexcept;
    bool decodejob_submit(DecodeJob&) noexcept;
    hcpp::size_t decodejob_pending() noexcept;
    bool pcie_decoder_available() noexcept;
    DecodeResult pcie_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept;
}  // namespace hype::video
