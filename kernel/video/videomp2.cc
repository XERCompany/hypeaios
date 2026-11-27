#include "kernel/video/videomp2.h"
namespace hype::video {
    namespace {
        struct BitReader {
            const hcpp::u8* data;
            hcpp::size_t size;
            hcpp::size_t pos;
            hcpp::u32 read(hcpp::u32 bits) noexcept {
                hcpp::u32 v = 0;
                for (hcpp::u32 i = 0; i < bits; ++i) {
                    const hcpp::size_t byte = pos >> 3;
                    const hcpp::u32 bit = 7 - (pos & 7);
                    v = (v << 1) | (byte < size ? (data[byte] >> bit) & 1 : 0);
                    ++pos;
                }
                return v;
            }
            void skip(hcpp::u32 bits) noexcept { pos += bits; }
        };
        const hcpp::u32 fps_num_table[] = {0, 24000, 24, 25, 30000, 30, 50, 60000, 60};
        const hcpp::u32 fps_den_table[] = {1, 1001, 1, 1, 1001, 1, 1, 1001, 1};
        const hcpp::u32 aspect_num_table[] = {0, 1, 4, 16, 221};
        const hcpp::u32 aspect_den_table[] = {1, 1, 3, 9, 100};
    }  // namespace
    bool mp2_packet_start(const hcpp::u8* p, hcpp::size_t n) noexcept {
        return n >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 1;
    }
    hcpp::size_t mp2_start_scan(const hcpp::u8* p, hcpp::size_t n, hcpp::size_t from,
                              hcpp::u8& code) noexcept {
        if (p == nullptr) return n;
        for (hcpp::size_t i = from; i + 3 < n; ++i) {
            if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
                code = p[i + 3];
                return i;
            }
        }
        return n;
    }
    bool mp2_seq_parse(const hcpp::u8* p, hcpp::size_t n, Mp2SeqInfo& out) noexcept {
        out = {};
        hcpp::u8 code = 0;
        const hcpp::size_t at = mp2_start_scan(p, n, 0, code);
        if (at >= n || code != mp2_start_sequence) return false;
        BitReader br{p + at + 4, n - at - 4, 0};
        if (br.size * 8 < 32) return false;
        out.width = br.read(12);
        out.height = br.read(12);
        const hcpp::u32 aspect = br.read(4);
        const hcpp::u32 frate = br.read(4);
        out.bitrate = br.read(18) * 400;
        br.skip(1);
        out.vbv = br.read(10);
        br.skip(1);
        if (br.read(1)) {
            if (br.pos + 512 + 1 > br.size * 8) return false;
            br.skip(512);
        }
        if (br.pos + 1 > br.size * 8) return false;
        if (br.read(1)) {
            if (br.pos + 512 > br.size * 8) return false;
            br.skip(512);
        }
        out.aspect_num = aspect < 5 ? aspect_num_table[aspect] : 0;
        out.aspect_den = aspect < 5 ? aspect_den_table[aspect] : 0;
        out.fps_num = frate < 9 ? fps_num_table[frate] : 0;
        out.fps_den = frate < 9 ? fps_den_table[frate] : 1;
        out.offset = at;
        out.valid = out.width != 0 && out.height != 0 && frate != 0 && frate < 9;
        return out.valid;
    }
    bool mp2_gop_parse(const hcpp::u8* p, hcpp::size_t n, Mp2GopInfo& out) noexcept {
        out = {};
        hcpp::u8 code = 0;
        hcpp::size_t from = 0;
        for (;;) {
            const hcpp::size_t at = mp2_start_scan(p, n, from, code);
            if (at >= n) return false;
            if (code == mp2_start_gop) {
                if (at + 8 > n) return false;
                BitReader br{p + at + 4, n - at - 4, 0};
                out.drop = br.read(1) != 0;
                out.hour = static_cast<hcpp::u8>(br.read(5));
                out.minute = static_cast<hcpp::u8>(br.read(6));
                br.skip(1);
                out.second = static_cast<hcpp::u8>(br.read(6));
                out.frame = static_cast<hcpp::u8>(br.read(6));
                out.closed = br.read(1) != 0;
                out.broken = br.read(1) != 0;
                return true;
            }
            from = at + 4;
        }
    }
    bool mp2_pic_parse(const hcpp::u8* p, hcpp::size_t n, Mp2PicInfo& out) noexcept {
        out = {};
        hcpp::u8 code = 0;
        hcpp::size_t from = 0;
        for (;;) {
            const hcpp::size_t at = mp2_start_scan(p, n, from, code);
            if (at >= n) return false;
            if (code == mp2_start_picture) {
                if (at + 6 > n) return false;
                BitReader br{p + at + 4, n - at - 4, 0};
                out.temporal_ref = static_cast<hcpp::u16>(br.read(10));
                out.coding_type = static_cast<hcpp::u8>(br.read(3));
                return out.coding_type >= 1 && out.coding_type <= 4;
            }
            from = at + 4;
        }
    }
    hcpp::size_t mp2_frame_boundaries(const hcpp::u8* p, hcpp::size_t n,
                                      hcpp::size_t* offsets,
                                      hcpp::size_t max) noexcept {
        hcpp::size_t count = 0, from = 0;
        hcpp::u8 code = 0;
        for (;;) {
            const hcpp::size_t at = mp2_start_scan(p, n, from, code);
            if (at >= n) break;
            if (code == mp2_start_picture && offsets != nullptr && count < max)
                offsets[count] = at;
            if (code == mp2_start_picture) ++count;
            from = at + 4;
        }
        return count;
    }
    DecodeResult mp2_probe(const hcpp::u8* p, hcpp::size_t n) noexcept {
        hcpp::u8 code = 0;
        const hcpp::size_t at = mp2_start_scan(p, n, 0, code);
        const bool ok = at < n && (code == mp2_start_sequence || code == mp2_start_picture ||
                                   code == mp2_start_gop);
        return {ok, 0, 0, CodecType::Mpeg2};
    }
}  // namespace hype::video
