#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    constexpr hcpp::u8 mp2_start_picture = 0x00;
    constexpr hcpp::u8 mp2_start_sequence = 0xB3;
    constexpr hcpp::u8 mp2_start_extension = 0xB5;
    constexpr hcpp::u8 mp2_start_sequence_end = 0xB7;
    constexpr hcpp::u8 mp2_start_gop = 0xB8;
    struct Mp2SeqInfo {
        hcpp::u32 width, height;
        hcpp::u32 aspect_num, aspect_den;
        hcpp::u32 fps_num, fps_den;
        hcpp::u32 bitrate;
        hcpp::u32 vbv;
        hcpp::size_t offset;
        bool valid;
    };
    struct Mp2GopInfo {
        hcpp::u8 hour, minute, second, frame;
        bool drop, closed, broken;
    };
    struct Mp2PicInfo {
        hcpp::u16 temporal_ref;
        hcpp::u8 coding_type;
    };
    bool mp2_packet_start(const hcpp::u8*, hcpp::size_t) noexcept;
    hcpp::size_t mp2_start_scan(const hcpp::u8*, hcpp::size_t, hcpp::size_t,
                              hcpp::u8&) noexcept;
    bool mp2_seq_parse(const hcpp::u8*, hcpp::size_t, Mp2SeqInfo&) noexcept;
    bool mp2_gop_parse(const hcpp::u8*, hcpp::size_t, Mp2GopInfo&) noexcept;
    bool mp2_pic_parse(const hcpp::u8*, hcpp::size_t, Mp2PicInfo&) noexcept;
    hcpp::size_t mp2_frame_boundaries(const hcpp::u8*, hcpp::size_t, hcpp::size_t*,
                                      hcpp::size_t) noexcept;
    DecodeResult mp2_probe(const hcpp::u8*, hcpp::size_t) noexcept;
}  // namespace hype::video
