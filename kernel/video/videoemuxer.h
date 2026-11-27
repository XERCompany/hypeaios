#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video {
    enum class DemuxKind : hcpp::u8 { Unknown, Avi, MpegTs, Ebml };
    enum class DemuxStreamType : hcpp::u8 { Unknown, Audio, Video };
    constexpr hcpp::size_t demux_max_streams = 8;
    struct DemuxStream {
        hcpp::u32 id;
        DemuxStreamType type;
        CodecType codec;
        hcpp::u32 fourcc;
        hcpp::u32 width, height;
    };
    struct DemuxPacket {
        const hcpp::u8* data;
        hcpp::size_t size;
        hcpp::u32 stream_id;
        DemuxStreamType type;
        CodecType codec;
        hcpp::u64 pts;
        bool has_pts;
    };
    struct StreamPacket {
        const hcpp::u8* data;
        hcpp::size_t size;
        hcpp::u32 stream;
        hcpp::u64 timestamp;
    };
    struct Demuxer {
        DemuxKind kind;
        const hcpp::u8* base;
        hcpp::size_t size;
        hcpp::size_t pos;
        hcpp::size_t data_end;
        hcpp::size_t cluster_end;
        DemuxStream streams[demux_max_streams];
        hcpp::size_t stream_count;
        hcpp::u32 video_stream_id;
        hcpp::u32 audio_stream_id;
        hcpp::u64 cluster_timecode;
        hcpp::u64 timecode_scale;
    };
    bool emuxer_probe_packet(const StreamPacket&) noexcept;
    CodecType emuxer_codec(const StreamPacket&) noexcept;
    bool emuxer_avi_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    bool emuxer_avi_mjpg_frame(const hcpp::u8*, hcpp::size_t, const hcpp::u8*&,
                               hcpp::size_t&) noexcept;
    DemuxKind demux_probe(const hcpp::u8*, hcpp::size_t) noexcept;
    const char* demux_kind_name(DemuxKind) noexcept;
    bool demux_open(Demuxer&, const hcpp::u8*, hcpp::size_t) noexcept;
    bool demux_packet(Demuxer&, DemuxPacket&) noexcept;
    hcpp::size_t demux_stream_count(const Demuxer&) noexcept;
    const DemuxStream* demux_stream_at(const Demuxer&, hcpp::size_t) noexcept;
    const DemuxStream* demux_stream_find(const Demuxer&, hcpp::u32) noexcept;
    hcpp::u32 fourcc(const char*) noexcept;
}  // namespace hype::video
