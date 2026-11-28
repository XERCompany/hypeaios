#include "kernel/codecapps/avi.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/video/videoemuxer.h"

// AVI codec entry: the RIFF LIST/hdrl + movi walk lives in
// kernel/video/videoemuxer.cc (shared with the v4lv path); this adapter
// routes the first decodable video packet through the codec pipeline —
// `MJPG`/`##dc` chunks go to the baseline JPEG decoder. Raw `DIB ` and
// other fourccs return Unsupported for now (strf lacks bpp in DemuxStream).

namespace hype::codec {

    CodecStatus avi_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (src == nullptr || frame == nullptr || size < 12)
            return CodecStatus::BadInput;
        video::Demuxer demux;
        if (!video::demux_open(demux, src, size) || demux.kind != video::DemuxKind::Avi)
            return CodecStatus::BadInput;
        video::DemuxPacket packet;
        while (video::demux_packet(demux, packet)) {
            if (packet.type != video::DemuxStreamType::Video || packet.data == nullptr ||
                packet.size == 0)
                continue;
            if (packet.codec == video::CodecType::Mjpeg)
                return image::jpeg_decode(packet.data, packet.size, frame, arena);
            return CodecStatus::Unsupported;
        }
        return CodecStatus::BadInput;
    }

    hcpp::u32 avi_frame_count(const hcpp::u8 *src, hcpp::size_t size) noexcept {
        video::Demuxer demux;
        if (src == nullptr || !video::demux_open(demux, src, size) ||
            demux.kind != video::DemuxKind::Avi)
            return 0;
        hcpp::u32 frames = 0;
        video::DemuxPacket packet;
        while (video::demux_packet(demux, packet))
            if (packet.type == video::DemuxStreamType::Video) ++frames;
        return frames;
    }

}  // namespace hype::codec
