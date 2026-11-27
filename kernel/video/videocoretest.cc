#include "kernel/video/videocoretest.h"

#include "kernel/codecapps/image/jpeg_selftest_blob.h"
#include "kernel/codecapps/registry.h"
#include "kernel/serial.hpp"
#include "kernel/video/videocapture.h"
#include "kernel/video/videoclient.h"
#include "kernel/video/videocontrol.h"
#include "kernel/video/videodecoderlz.h"
#include "kernel/video/videodecoderpcie.h"
#include "kernel/video/videoemuxer.h"
#include "kernel/video/videoformat.h"
#include "kernel/video/videomcodec.h"
#include "kernel/video/videomp2.h"
#include "kernel/video/videoraw.h"
#include "kernel/video/videostream.h"
#include "kernel/video/videov4lv_key.h"
#include "kernel/video/videov4lvstate.h"
#include "kernel/video/videovirtual.h"
#include "kernel/video/videoyuv.h"
namespace hype::video {
    namespace {
        hcpp::u8 rgb_out[vcap_width * vcap_height * 4];
        const hcpp::u8 expected_rgb[vcap_bars][3] = {{255, 255, 255}, {255, 255, 0},   {0, 255, 255},
                                                   {0, 255, 0},     {255, 0, 255},   {255, 0, 0},
                                                   {0, 0, 255},     {0, 0, 0}};
        bool close_enough(hcpp::u8 a, hcpp::u8 b) noexcept {
            const int d = int(a) - int(b);
            return d > -6 && d < 6;
        }
        hcpp::u32 decode_counter(const VideoFrame& f) noexcept {
            hcpp::u32 v = 0;
            for (hcpp::u32 bit = 0; bit < 8; ++bit)
                if (f.data[4 * f.width + bit * 8 + 4] > 127) v |= 1u << bit;
            return v;
        }
        bool verify_bars(const VideoFrame& f) noexcept {
            const hcpp::u32 y = f.height / 3;
            for (hcpp::u32 bar = 0; bar < vcap_bars; ++bar) {
                const hcpp::u32 x = bar * (f.width / vcap_bars) + f.width / (vcap_bars * 2);
                const hcpp::u8* p = f.data + (y * f.width + x) * 4;
                if (!close_enough(p[0], expected_rgb[bar][0]) ||
                    !close_enough(p[1], expected_rgb[bar][1]) ||
                    !close_enough(p[2], expected_rgb[bar][2]))
                    return false;
            }
            return true;
        }
    }  // namespace
        hcpp::u8 lzf_src[16 * 8 * 4];
        hcpp::u8 lzf_dst[16 * 8 * 4];
        hcpp::u8 lzf_enc[16 * 8 * 4 * 2 + 64];
        hcpp::u8 mjpeg_px[24 * 16 * 4];
        hcpp::u8 avi_buf[2048];
        hcpp::u8 raw_rgb[8 * 4 * 4];
        hcpp::u8 yuy2_src[4 * 2 * 2];

        void mark(const char* name, bool ok) noexcept {
            serial::write(name);
            serial::write_line(ok ? "ok" : "fail");
        }
        bool bytes_eq(const hcpp::u8* a, const hcpp::u8* b, hcpp::size_t n) noexcept {
            for (hcpp::size_t i = 0; i < n; ++i)
                if (a[i] != b[i]) return false;
            return true;
        }
        void wr32(hcpp::u8* p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v & 0xFF);
            p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
            p[2] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
            p[3] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
        }
        void tag(hcpp::u8* p, char a, char b, char c, char d) noexcept {
            p[0] = static_cast<hcpp::u8>(a);
            p[1] = static_cast<hcpp::u8>(b);
            p[2] = static_cast<hcpp::u8>(c);
            p[3] = static_cast<hcpp::u8>(d);
        }

        hcpp::size_t lzf_sample(hcpp::size_t& out_len) noexcept {
            for (hcpp::u32 y = 0; y < 8; ++y)
                for (hcpp::u32 x = 0; x < 16; ++x) {
                    hcpp::u8* p = lzf_src + (hcpp::size_t(y) * 16 + x) * 4;
                    p[0] = static_cast<hcpp::u8>((x / 4) * 40);
                    p[1] = static_cast<hcpp::u8>(x * 4);
                    p[2] = static_cast<hcpp::u8>(y * 16);
                    p[3] = 0xFF;
                }
            VideoFrame in{16, 8, 64, PixelFormat::RGB32, lzf_src, sizeof(lzf_src), 0};
            out_len = lzf_encode_frame(in, lzf_enc, sizeof(lzf_enc));
            return out_len;
        }
        bool lzf_roundtrip() noexcept {
            hcpp::size_t n = 0;
            if (lzf_sample(n) == 0 || !lzf_probe(lzf_enc, n)) return false;
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            const DecodeResult r = lz_decode_frame(lzf_enc, n, out);
            return r.ok && r.produced == sizeof(lzf_src) && out.width == 16 &&
                   out.height == 8 && out.format == PixelFormat::RGB32 &&
                   bytes_eq(lzf_src, lzf_dst, sizeof(lzf_src));
        }
        bool ffv1_stub_ok() noexcept {
            const hcpp::u8 tag4[] = {'F', 'F', 'V', '1'};
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            const DecodeResult r = ffv1_decode_frame(tag4, sizeof(tag4), out);
            return ffv1_probe(tag4, sizeof(tag4)) && !r.ok && r.codec == CodecType::Ffv1;
        }
        hcpp::size_t avi_build() noexcept {
            const hcpp::size_t jlen = sizeof(jpeg_selftest_blob);
            const hcpp::size_t pad = jlen & 1;
            hcpp::size_t p = 0;
            tag(avi_buf + p, 'R', 'I', 'F', 'F');
            wr32(avi_buf + p + 4, 0);
            tag(avi_buf + p + 8, 'A', 'V', 'I', ' ');
            p += 12;
            tag(avi_buf + p, 'L', 'I', 'S', 'T');
            wr32(avi_buf + p + 4, 4 + 8 + 56);
            tag(avi_buf + p + 8, 'h', 'd', 'r', 'l');
            p += 12;
            tag(avi_buf + p, 's', 't', 'r', 'h');
            wr32(avi_buf + p + 4, 56);
            tag(avi_buf + p + 8, 'v', 'i', 'd', 's');
            tag(avi_buf + p + 12, 'M', 'J', 'P', 'G');
            for (hcpp::u32 k = 16; k < 56; ++k) avi_buf[p + 8 + k] = 0;
            p += 8 + 56;
            tag(avi_buf + p, 'L', 'I', 'S', 'T');
            wr32(avi_buf + p + 4, static_cast<hcpp::u32>(4 + 8 + jlen + pad));
            tag(avi_buf + p + 8, 'm', 'o', 'v', 'i');
            p += 12;
            tag(avi_buf + p, '0', '0', 'd', 'c');
            wr32(avi_buf + p + 4, static_cast<hcpp::u32>(jlen));
            p += 8;
            for (hcpp::size_t k = 0; k < jlen; ++k) avi_buf[p + k] = jpeg_selftest_blob[k];
            p += jlen;
            if (pad) avi_buf[p++] = 0;
            wr32(avi_buf + 4, static_cast<hcpp::u32>(p - 8));
            return p;
        }
        bool mjpeg_ok() noexcept {
            VideoFrame f{0, 0, 0, PixelFormat::Unknown, mjpeg_px, sizeof(mjpeg_px), 0};
            DecodeResult r = mjpeg_decode_frame(jpeg_selftest_blob, sizeof(jpeg_selftest_blob), f);
            if (!r.ok || f.width != JPEG_SELFTEST_WIDTH || f.height != JPEG_SELFTEST_HEIGHT ||
                f.format != PixelFormat::RGB32)
                return false;
            const hcpp::size_t avi_len = avi_build();
            if (!emuxer_avi_probe(avi_buf, avi_len)) return false;
            const hcpp::u8* fr = nullptr;
            hcpp::size_t fl = 0;
            if (!emuxer_avi_mjpg_frame(avi_buf, avi_len, fr, fl)) return false;
            if (fl != sizeof(jpeg_selftest_blob) || !bytes_eq(fr, jpeg_selftest_blob, fl))
                return false;
            VideoFrame f2{0, 0, 0, PixelFormat::Unknown, mjpeg_px, sizeof(mjpeg_px), 0};
            r = mjpeg_decode_frame(fr, fl, f2);
            return r.ok && f2.width == JPEG_SELFTEST_WIDTH &&
                   f2.height == JPEG_SELFTEST_HEIGHT;
        }
        bool raw_ok() noexcept {
            for (hcpp::size_t i = 0; i < sizeof(raw_rgb); ++i) raw_rgb[i] = 0;
            const hcpp::u8 rgb_src[4 * 2 * 3] = {10, 20, 30, 40, 50, 60, 70, 80,
                                                 90, 100, 110, 120, 1, 2, 3, 4,
                                                 5, 6, 7, 8, 9, 10, 11, 12};
            VideoFrame rgb{0, 0, 0, PixelFormat::RGB24, raw_rgb, 4 * 2 * 3, 0};
            if (!raw_frame_unpack(rgb_src, sizeof(rgb_src), 4, 2, PixelFormat::RGB24, rgb))
                return false;
            if (rgb.width != 4 || rgb.height != 2 || rgb.size != 24 ||
                !bytes_eq(raw_rgb, rgb_src, 24))
                return false;
            for (hcpp::size_t i = 0; i < sizeof(raw_rgb); ++i) raw_rgb[i] = 0;
            yuy2_src[0] = 81;
            yuy2_src[1] = 90;
            yuy2_src[2] = 81;
            yuy2_src[3] = 240;
            for (hcpp::size_t i = 4; i < sizeof(yuy2_src); ++i)
                yuy2_src[i] = static_cast<hcpp::u8>(i);
            VideoFrame yuv{0, 0, 0, PixelFormat::RGB32, raw_rgb, sizeof(raw_rgb), 0};
            if (!raw_frame_unpack(yuy2_src, sizeof(yuy2_src), 4, 2, PixelFormat::YUY2, yuv))
                return false;
            if (yuv.width != 4 || yuv.height != 2 || yuv.format != PixelFormat::RGB32)
                return false;
            return raw_rgb[0] == 238 && raw_rgb[1] == 14 && raw_rgb[2] == 13 &&
                   raw_rgb[3] == 0xFF;
        }
        bool decodejob_ok() noexcept {
            hcpp::size_t n = 0;
            if (lzf_sample(n) == 0) return false;
            VideoFrame out{0, 0, 0, PixelFormat::Unknown, lzf_dst, sizeof(lzf_dst), 0};
            DecodeJob j{CodecType::Lz, lzf_enc, n, &out, 0, DecodeJobStatus::Free, {}};
            if (!decodejob_submit(j) || j.status != DecodeJobStatus::Done || !j.result.ok)
                return false;
            if (!bytes_eq(lzf_src, lzf_dst, sizeof(lzf_src))) return false;
            DecodeJob hw{CodecType::Mjpeg, jpeg_selftest_blob, sizeof(jpeg_selftest_blob),
                         &out, decodejob_flag_hw, DecodeJobStatus::Free, {}};
            if (!decodejob_submit(hw) || hw.status != DecodeJobStatus::Unsupported) return false;
            return !pcie_decoder_available() && decodejob_pending() == 0;
        }
    namespace {
        struct BlobWriter {
            hcpp::u8* b;
            hcpp::size_t p;
            void u8(hcpp::u8 v) noexcept { b[p++] = v; }
            void u16(hcpp::u16 v) noexcept {
                u8(static_cast<hcpp::u8>(v));
                u8(static_cast<hcpp::u8>(v >> 8));
            }
            void u32(hcpp::u32 v) noexcept {
                u8(static_cast<hcpp::u8>(v));
                u8(static_cast<hcpp::u8>(v >> 8));
                u8(static_cast<hcpp::u8>(v >> 16));
                u8(static_cast<hcpp::u8>(v >> 24));
            }
            void tag(const char* t) noexcept {
                for (hcpp::u32 i = 0; i < 4; ++i) u8(static_cast<hcpp::u8>(t[i]));
            }
            void zero(hcpp::size_t n) noexcept {
                while (n--) u8(0);
            }
        };
        hcpp::size_t build_avi(hcpp::u8* b) noexcept {
            BlobWriter w{b, 0};
            w.tag("RIFF");
            w.u32(264);
            w.tag("AVI ");
            w.tag("LIST");
            w.u32(192);
            w.tag("hdrl");
            w.tag("avih");
            w.u32(56);
            w.u32(33333);
            w.zero(52);
            w.tag("LIST");
            w.u32(116);
            w.tag("strl");
            w.tag("strh");
            w.u32(56);
            w.tag("vids");
            w.tag("MJPG");
            w.u32(0);
            w.u32(0);
            w.u32(1);
            w.u32(30);
            w.u32(0);
            w.u32(2);
            w.zero(24);
            w.tag("strf");
            w.u32(40);
            w.u32(40);
            w.u32(160);
            w.u32(120);
            w.u16(1);
            w.u16(24);
            w.tag("MJPG");
            w.u32(160 * 120 * 3);
            w.zero(16);
            w.tag("LIST");
            w.u32(52);
            w.tag("movi");
            w.tag("00dc");
            w.u32(4);
            w.tag("FRA1");
            w.tag("JUNK");
            w.u32(4);
            w.zero(4);
            w.tag("00dc");
            w.u32(4);
            w.tag("FRA2");
            w.tag("01wb");
            w.u32(4);
            w.tag("AUD1");
            return w.p;
        }
        bool demux_avi_check() noexcept {
            static hcpp::u8 blob[280];
            const hcpp::size_t n = build_avi(blob);
            if (demux_probe(blob, n) != DemuxKind::Avi) return false;
            Demuxer d;
            if (!demux_open(d, blob, n)) return false;
            if (demux_stream_count(d) != 1) return false;
            const DemuxStream* s = demux_stream_at(d, 0);
            if (s == nullptr || s->type != DemuxStreamType::Video ||
                s->codec != CodecType::Mjpeg || s->width != 160 || s->height != 120)
                return false;
            DemuxPacket p;
            if (!demux_packet(d, p) || p.type != DemuxStreamType::Video ||
                p.stream_id != 0 || p.size != 4 || p.codec != CodecType::Mjpeg)
                return false;
            if (!demux_packet(d, p) || p.stream_id != 0 || p.size != 4) return false;
            if (!demux_packet(d, p) || p.type != DemuxStreamType::Audio ||
                p.stream_id != 1)
                return false;
            return !demux_packet(d, p);
        }
        hcpp::size_t build_ts(hcpp::u8* b) noexcept {
            for (hcpp::size_t i = 0; i < 564; ++i) b[i] = 0xFF;
            hcpp::u8* pat = b;
            pat[0] = 0x47;
            pat[1] = 0x40;
            pat[2] = 0x00;
            pat[3] = 0x10;
            pat[4] = 0x00;
            const hcpp::u8 pat_tab[] = {0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00,
                                        0x00, 0x00, 0x01, 0xE1, 0x00, 0x00,
                                        0x00, 0x00, 0x00};
            for (hcpp::size_t i = 0; i < sizeof(pat_tab); ++i) pat[5 + i] = pat_tab[i];
            hcpp::u8* pmt = b + 188;
            pmt[0] = 0x47;
            pmt[1] = 0x41;
            pmt[2] = 0x00;
            pmt[3] = 0x10;
            pmt[4] = 0x00;
            const hcpp::u8 pmt_tab[] = {0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00,
                                        0x00, 0xE1, 0x01, 0xF0, 0x00, 0x02, 0xE1,
                                        0x01, 0xF0, 0x00, 0x04, 0xE1, 0x02, 0xF0,
                                        0x00, 0x00, 0x00, 0x00, 0x00};
            for (hcpp::size_t i = 0; i < sizeof(pmt_tab); ++i) pmt[5 + i] = pmt_tab[i];
            hcpp::u8* pes = b + 376;
            pes[0] = 0x47;
            pes[1] = 0x41;
            pes[2] = 0x01;
            pes[3] = 0x10;
            const hcpp::u8 pes_hdr[] = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x0D, 0x80,
                                        0x80, 0x05, 0x21, 0x00, 0x05, 0xBF, 0x21,
                                        'M',  'P',  'G',  '2'};
            for (hcpp::size_t i = 0; i < sizeof(pes_hdr); ++i) pes[4 + i] = pes_hdr[i];
            return 564;
        }
        bool demux_ts_check() noexcept {
            static hcpp::u8 blob[564];
            const hcpp::size_t n = build_ts(blob);
            if (demux_probe(blob, n) != DemuxKind::MpegTs) return false;
            Demuxer d;
            if (!demux_open(d, blob, n)) return false;
            if (demux_stream_count(d) != 2) return false;
            const DemuxStream* v = demux_stream_find(d, 0x101);
            const DemuxStream* a = demux_stream_find(d, 0x102);
            if (v == nullptr || v->type != DemuxStreamType::Video ||
                v->codec != CodecType::Mpeg2)
                return false;
            if (a == nullptr || a->type != DemuxStreamType::Audio) return false;
            DemuxPacket p;
            if (!demux_packet(d, p) || p.stream_id != 0x101 ||
                p.type != DemuxStreamType::Video || p.codec != CodecType::Mpeg2)
                return false;
            if (!p.has_pts || p.pts != 90000) return false;
            return !demux_packet(d, p);
        }
        hcpp::size_t build_ebml(hcpp::u8* b) noexcept {
            BlobWriter w{b, 0};
            const hcpp::u8 head[] = {0x1A, 0x45, 0xDF, 0xA3, 0x80};
            for (hcpp::size_t i = 0; i < sizeof(head); ++i) w.u8(head[i]);
            const hcpp::u8 seg[] = {0x18, 0x53, 0x80, 0x67, 0xBE};
            for (hcpp::size_t i = 0; i < sizeof(seg); ++i) w.u8(seg[i]);
            const hcpp::u8 info[] = {0x15, 0x49, 0xA9, 0x66, 0x87,
                                     0x2A, 0xD7, 0xB1, 0x83, 0x0F, 0x42, 0x40};
            for (hcpp::size_t i = 0; i < sizeof(info); ++i) w.u8(info[i]);
            const hcpp::u8 tracks[] = {
                0x16, 0x54, 0xAE, 0x6B, 0x9A, 0xAE, 0x98, 0xD7, 0x81, 0x01, 0x83,
                0x81, 0x01, 0x86, 0x87, 'V',  '_',  'M',  'P',  'E',  'G',  '2',
                0xE0, 0x87, 0xB0, 0x82, 0x01, 0x40, 0xBA, 0x81, 0x78};
            for (hcpp::size_t i = 0; i < sizeof(tracks); ++i) w.u8(tracks[i]);
            const hcpp::u8 cluster[] = {0x1F, 0x43, 0xB6, 0x75, 0x8E, 0xE7, 0x82,
                                        0x00, 0x00, 0xA3, 0x88, 0x81, 0x00, 0x00,
                                        0x80, 'M',  'P',  'G',  '2'};
            for (hcpp::size_t i = 0; i < sizeof(cluster); ++i) w.u8(cluster[i]);
            return w.p;
        }
        bool demux_ebml_check() noexcept {
            static hcpp::u8 blob[128];
            const hcpp::size_t n = build_ebml(blob);
            if (demux_probe(blob, n) != DemuxKind::Ebml) return false;
            Demuxer d;
            if (!demux_open(d, blob, n)) return false;
            if (demux_stream_count(d) != 1) return false;
            const DemuxStream* s = demux_stream_at(d, 0);
            if (s == nullptr || s->type != DemuxStreamType::Video ||
                s->codec != CodecType::Mpeg2 || s->width != 320 || s->height != 120)
                return false;
            if (d.timecode_scale != 1000000) return false;
            DemuxPacket p;
            if (!demux_packet(d, p) || p.stream_id != 1 ||
                p.type != DemuxStreamType::Video || p.codec != CodecType::Mpeg2 ||
                p.size != 4)
                return false;
            return !demux_packet(d, p);
        }
        const hcpp::u8 mp2_blob[] = {0x00, 0x00, 0x01, 0xB3, 0x0A, 0x00, 0x78, 0x23,
                                     0xFF, 0xFF, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x01,
                                     0xB8, 0x08, 0x00, 0x04, 0x40, 0x00, 0x00, 0x01,
                                     0x00, 0x00, 0x08, 0xFF, 0xFF};
        bool demux_selftest_run() noexcept {
            return demux_avi_check() && demux_ts_check() && demux_ebml_check();
        }
    }  // namespace
    bool video_demux_selftest() noexcept {
        const bool demux_ok = demux_selftest_run();
        serial::write("demux avi/ts/ebml=");
        serial::write_line(demux_ok ? "ok" : "fail");
        Mp2SeqInfo seq{};
        const bool seq_ok = mp2_seq_parse(mp2_blob, sizeof(mp2_blob), seq);
        serial::write("mp2 seq=");
        serial::write_dec32(seq_ok ? seq.width : 0);
        serial::write_char('x');
        serial::write_dec32(seq_ok ? seq.height : 0);
        serial::write("\r\n");
        hcpp::size_t spans[4];
        const bool frames_ok = mp2_frame_boundaries(mp2_blob, sizeof(mp2_blob), spans,
                                                    4) == 1 &&
                               seq_ok && seq.fps_num == 25 && seq.fps_den == 1;
        hcpp::size_t video_codecs = 0;
        for (hcpp::size_t i = 0; codec::codec_at(i) != nullptr; ++i)
            if (codec::codec_at(i)->kind == codec::CodecKind::Video) ++video_codecs;
        serial::write("video codec=");
        serial::write_dec32(static_cast<hcpp::u32>(video_codecs));
        serial::write("\r\n");
        return demux_ok && frames_ok && video_codecs != 0 && mcodec_count() != 0;
    }
    bool video_core_selftest() noexcept {
        bool ok = true;
        const hcpp::i32 client = video_open_dev(0);
        bool cap = client >= 0 && video_req_bufs(client, 4) && !video_qbuf(client) && v4lv_stream_on(0) &&
                   !v4lv_stream_on(0);
        VideoFrame frames[3]{};
        hcpp::u64 base = 0;
        for (hcpp::u32 i = 0; i < 3 && cap; ++i) {
            cap = video_qbuf(client) && video_dqbuf(client, frames[i]) && frame_valid(frames[i]);
            if (cap && i == 0) base = frames[i].timestamp;
            cap = cap && frames[i].timestamp == base + i && decode_counter(frames[i]) == (base + i);
        }
        serial::write("vcap frame=");
        serial::write_line(cap ? "ok" : "fail");
        ok = ok && cap;
        VideoFrame rgb{vcap_width, vcap_height, vcap_width * 4, PixelFormat::RGB32, rgb_out, sizeof(rgb_out), 0};
        const bool conv = nv12_to_rgb32(frames[0], rgb) && frame_valid(rgb) && verify_bars(rgb);
        serial::write("yuv conv=");
        serial::write_line(conv ? "ok" : "fail");
        ok = ok && conv;
        VideoFrame scratch{};
        bool strm = v4lv_stream_off(0) && v4lv_state(0) == VideoDeviceState::Open &&
                    !video_dqbuf(client, scratch) && !video_qbuf(client) && v4lv_stream_on(0);
        for (hcpp::u32 i = 0; i < video_stream_max_buffers + 1 && strm; ++i) strm = video_qbuf(client);
        const VideoStream* s = v4lv_stream(0);
        strm = strm && s != nullptr && stream_pending(*s) == video_stream_max_buffers && s->dropped != 0;
        strm = strm && stream_pop(*v4lv_stream(0), scratch) && scratch.timestamp == base + 3 + 1;
        while (stream_pop(*v4lv_stream(0), scratch)) {
        }
        strm = strm && v4lv_stream_off(0) && v4lv_state(0) == VideoDeviceState::Open;
        serial::write("v4lv stream=");
        serial::write_line(strm ? "ok" : "fail");
        ok = ok && strm;
        hcpp::i32 v = 0;
        const bool ctrl = control_set(0, VideoControlId::Brightness, 999) &&
                          control_get(0, VideoControlId::Brightness, v) && v == 127 &&
                          control_set(0, VideoControlId::Brightness, -999) &&
                          control_get(0, VideoControlId::Brightness, v) && v == -128 &&
                          control_get(0, VideoControlId::Saturation, v) && v == 128;
        serial::write("video ctrl=");
        serial::write_line(ctrl ? "ok" : "fail");
        ok = ok && ctrl;
        VideoDeviceInfo info{};
        const bool fmt = format_negotiate(info, 640, 480, PixelFormat::RGB24) &&
                         format_find(PixelFormat::NV12) != nullptr &&
                         format_find(PixelFormat::NV12)->fourcc == v4lv_key_nv12;
        serial::write("video fmt=");
        serial::write_line(fmt ? "ok" : "fail");
        ok = ok && fmt;
        serial::write("video core=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }
    bool video_decode_selftest() noexcept {
        bool ok = true;
        bool good = lzf_roundtrip();
        mark("lzf rt=", good);
        ok = ok && good;
        good = ffv1_stub_ok();
        serial::write("ffv1=");
        serial::write_line(good ? "stub" : "fail");
        ok = ok && good;
        good = mjpeg_ok();
        mark("mjpeg=", good);
        ok = ok && good;
        good = raw_ok();
        mark("raw frame=", good);
        ok = ok && good;
        good = decodejob_ok();
        mark("decodejob=", good);
        ok = ok && good;
        return ok;
    }
}  // namespace hype::video
