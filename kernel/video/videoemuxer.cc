#include "kernel/video/videoemuxer.h"

#include "kernel/video/videomp2.h"
namespace hype::video {
    namespace {
        hcpp::u32 rd32(const hcpp::u8* p) noexcept {
            return hcpp::u32(p[0]) | (hcpp::u32(p[1]) << 8) | (hcpp::u32(p[2]) << 16) |
                   (hcpp::u32(p[3]) << 24);
        }
        constexpr hcpp::u32 tag_riff = 0x46464952;
        constexpr hcpp::u32 tag_avi = 0x20495641;
        constexpr hcpp::u32 tag_list = 0x5453494C;
        constexpr hcpp::u32 tag_movi = 0x69766F6D;
        constexpr hcpp::u32 tag_mjpg = 0x47504A4D;
        constexpr hcpp::u32 tag_00dc = 0x63643030;
        constexpr hcpp::u32 tag_00db = 0x62643030;
    }
    bool emuxer_probe_packet(const StreamPacket& p) noexcept { return p.data && p.size; }
    CodecType emuxer_codec(const StreamPacket& p) noexcept {
        if (p.data == nullptr || p.size < 4) return CodecType::Unknown;
        if (mp2_packet_start(p.data, p.size)) return CodecType::Mpeg2;
        if (mjpeg_probe(p.data, p.size)) return CodecType::Mjpeg;
        if (emuxer_avi_probe(p.data, p.size)) return CodecType::Mjpeg;
        return CodecType::Unknown;
    }
    bool emuxer_avi_probe(const hcpp::u8* src, hcpp::size_t n) noexcept {
        if (src == nullptr || n < 12) return false;
        if (rd32(src) != tag_riff || rd32(src + 8) != tag_avi) return false;
        const hcpp::size_t scan = n < 2048 ? n : 2048;
        for (hcpp::size_t i = 12; i + 4 <= scan; ++i)
            if (rd32(src + i) == tag_mjpg) return true;
        return false;
    }
    bool emuxer_avi_mjpg_frame(const hcpp::u8* src, hcpp::size_t n, const hcpp::u8*& frame,
                               hcpp::size_t& frame_len) noexcept {
        frame = nullptr;
        frame_len = 0;
        if (!emuxer_avi_probe(src, n)) return false;
        hcpp::size_t i = 12;
        while (i + 8 <= n) {
            const hcpp::u32 id = rd32(src + i);
            const hcpp::u32 sz = rd32(src + i + 4);
            const hcpp::size_t body = i + 8;
            const hcpp::size_t end = body + sz < n ? body + sz : n;
            if (id == tag_list && body + 4 <= n && rd32(src + body) == tag_movi) {
                hcpp::size_t j = body + 4;
                while (j + 8 <= end) {
                    const hcpp::u32 cid = rd32(src + j);
                    const hcpp::u32 csz = rd32(src + j + 4);
                    if ((cid == tag_00dc || cid == tag_00db) && csz != 0 && j + 8 + csz <= n &&
                        mjpeg_probe(src + j + 8, csz)) {
                        frame = src + j + 8;
                        frame_len = csz;
                        return true;
                    }
                    j += 8 + csz + (csz & 1);
                }
                return false;
            }
            i += 8 + sz + (sz & 1);
        }
        return false;
    }
}  // namespace hype::video
