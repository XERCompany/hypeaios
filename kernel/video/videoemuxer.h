#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    struct StreamPacket {
        const hcpp::u8* data;
        hcpp::size_t size;
        hcpp::u32 stream;
        hcpp::u64 timestamp;
    };
    bool emuxer_probe_packet(const StreamPacket&) noexcept;
    CodecType emuxer_codec(const StreamPacket&) noexcept;
    bool emuxer_avi_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    bool emuxer_avi_mjpg_frame(const hcpp::u8*, hcpp::size_t, const hcpp::u8*&,
                               hcpp::size_t&) noexcept;
}  // namespace hype::video
