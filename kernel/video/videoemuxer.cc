#include "kernel/video/videoemuxer.h"

#include "kernel/video/videomp2.h"
namespace hype::video {
    namespace {
        hcpp::u16 rd16(const hcpp::u8* p) noexcept {
            return static_cast<hcpp::u16>(p[0] | (static_cast<hcpp::u16>(p[1]) << 8));
        }
        hcpp::u32 rd32(const hcpp::u8* p) noexcept {
            return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }
        bool tag_eq(const hcpp::u8* p, const char* t) noexcept {
            return p[0] == static_cast<hcpp::u8>(t[0]) &&
                   p[1] == static_cast<hcpp::u8>(t[1]) &&
                   p[2] == static_cast<hcpp::u8>(t[2]) && p[3] == static_cast<hcpp::u8>(t[3]);
        }
        bool ascii_eq(const hcpp::u8* p, hcpp::size_t n, const char* s) noexcept {
            hcpp::size_t len = 0;
            while (s[len] != '\0') ++len;
            if (len != n) return false;
            for (hcpp::size_t i = 0; i < n; ++i)
                if (p[i] != static_cast<hcpp::u8>(s[i])) return false;
            return true;
        }
        bool fourcc_in(hcpp::u32 f, const char* const* list, hcpp::size_t count) noexcept {
            for (hcpp::size_t i = 0; i < count; ++i)
                if (f == fourcc(list[i])) return true;
            return false;
        }
        CodecType video_codec_by_fourcc(hcpp::u32 f) noexcept {
            const char* mjpeg[] = {"MJPG", "mjpg", "JPEG", "jpeg", "LJPG", "dmb1",
                                   "MJPA", "AVRn", "CJPG", "ACDV", "SLMJ"};
            const char* mpeg[] = {"MPG2", "MPG1", "MPEG", "MP2V", "MP1V",
                                  "EM2V", "M702", "LMP2", "MMES", "mpg2", "mpg1"};
            const char* avc[] = {"avc1", "AVC1", "H264", "h264", "X264",
                                 "x264", "DAVC", "VSSH"};
            if (f == 0 || f == fourcc("DIB ") || f == fourcc("RGB ") ||
                f == fourcc("RAW "))
                return CodecType::Raw;
            if (fourcc_in(f, mjpeg, sizeof(mjpeg) / sizeof(mjpeg[0])))
                return CodecType::Mjpeg;
            if (fourcc_in(f, mpeg, sizeof(mpeg) / sizeof(mpeg[0])))
                return CodecType::Mpeg2;
            if (fourcc_in(f, avc, sizeof(avc) / sizeof(avc[0])))
                return CodecType::H264;
            return CodecType::Unknown;
        }
        CodecType mkv_codec(const hcpp::u8* p, hcpp::size_t n, const hcpp::u8* priv,
                            hcpp::size_t priv_n) noexcept {
            if (ascii_eq(p, n, "V_MPEG2") || ascii_eq(p, n, "V_MPEG1"))
                return CodecType::Mpeg2;
            if (ascii_eq(p, n, "V_MJPEG")) return CodecType::Mjpeg;
            if (ascii_eq(p, n, "V_MPEG4/ISO/AVC")) return CodecType::H264;
            if (ascii_eq(p, n, "V_MS/VFW/FOURCC") && priv != nullptr && priv_n >= 20)
                return video_codec_by_fourcc(rd32(priv + 16));
            return CodecType::Unknown;
        }
        DemuxStream* add_stream(Demuxer& d, hcpp::u32 id, DemuxStreamType t,
                              CodecType c, hcpp::u32 fcc, hcpp::u32 w,
                              hcpp::u32 h) noexcept {
            if (d.stream_count >= demux_max_streams) return nullptr;
            DemuxStream* s = &d.streams[d.stream_count++];
            *s = {id, t, c, fcc, w, h};
            if (t == DemuxStreamType::Video) d.video_stream_id = id;
            if (t == DemuxStreamType::Audio) d.audio_stream_id = id;
            return s;
        }
        void avi_strl(Demuxer& d, hcpp::size_t s, hcpp::size_t e) noexcept {
            DemuxStreamType type = DemuxStreamType::Unknown;
            hcpp::u32 handler = 0, comp = 0, w = 0, h = 0;
            hcpp::u16 audio_tag = 0;
            hcpp::size_t p = s;
            while (p + 8 <= e) {
                const hcpp::u32 id = rd32(d.base + p);
                const hcpp::u32 sz = rd32(d.base + p + 4);
                const hcpp::size_t body = p + 8;
                if (id == fourcc("strh") && sz >= 8 && body + 8 <= e) {
                    if (tag_eq(d.base + body, "vids")) type = DemuxStreamType::Video;
                    else if (tag_eq(d.base + body, "auds")) type = DemuxStreamType::Audio;
                    handler = rd32(d.base + body + 4);
                } else if (id == fourcc("strf") && body + sz <= e) {
                    if (type == DemuxStreamType::Video && sz >= 20) {
                        w = rd32(d.base + body + 4);
                        h = rd32(d.base + body + 8);
                        comp = rd32(d.base + body + 16);
                    } else if (type == DemuxStreamType::Audio && sz >= 2) {
                        audio_tag = rd16(d.base + body);
                    }
                }
                p = body + sz + (sz & 1);
            }
            const hcpp::u32 fcc = comp != 0 ? comp : handler;
            CodecType codec = CodecType::Unknown;
            if (type == DemuxStreamType::Video) codec = video_codec_by_fourcc(fcc);
            add_stream(d, static_cast<hcpp::u32>(d.stream_count), type, codec,
                       fcc != 0 ? fcc : audio_tag, w, h);
        }
        void avi_hdrl(Demuxer& d, hcpp::size_t s, hcpp::size_t e) noexcept {
            hcpp::size_t p = s;
            while (p + 8 <= e) {
                const hcpp::u32 id = rd32(d.base + p);
                const hcpp::u32 sz = rd32(d.base + p + 4);
                const hcpp::size_t body = p + 8;
                const hcpp::size_t next = body + sz + (sz & 1);
                if (id == fourcc("LIST") && sz >= 4 && body + 4 <= e &&
                    tag_eq(d.base + body, "strl"))
                    avi_strl(d, body + 4, body + sz <= e ? body + sz : e);
                p = next;
            }
        }
        bool avi_open(Demuxer& d) noexcept {
            const hcpp::size_t end =
                d.size >= 8 && rd32(d.base + 4) + 8 <= d.size ? rd32(d.base + 4) + 8 : d.size;
            hcpp::size_t p = 12;
            while (p + 8 <= end) {
                const hcpp::u32 id = rd32(d.base + p);
                const hcpp::u32 sz = rd32(d.base + p + 4);
                const hcpp::size_t body = p + 8;
                const hcpp::size_t stop = body + sz <= end ? body + sz : end;
                if (id == fourcc("LIST") && sz >= 4 && body + 4 <= end) {
                    if (tag_eq(d.base + body, "hdrl"))
                        avi_hdrl(d, body + 4, stop);
                    else if (tag_eq(d.base + body, "movi")) {
                        d.pos = body + 4;
                        d.data_end = stop;
                        return true;
                    }
                }
                p = stop + (sz & 1);
            }
            return false;
        }
        bool avi_packet(Demuxer& d, DemuxPacket& pkt) noexcept {
            while (d.pos + 8 <= d.data_end) {
                const hcpp::u32 id = rd32(d.base + d.pos);
                const hcpp::u32 sz = rd32(d.base + d.pos + 4);
                const hcpp::size_t body = d.pos + 8;
                const hcpp::size_t stop =
                    body + sz <= d.data_end ? body + sz : d.data_end;
                if (id == fourcc("LIST") && sz >= 4) {
                    d.pos = body + 4;
                    continue;
                }
                d.pos = body + sz + (sz & 1);
                if (id == fourcc("JUNK") || sz == 0) continue;
                const hcpp::u8 c0 = static_cast<hcpp::u8>(id);
                const hcpp::u8 c1 = static_cast<hcpp::u8>(id >> 8);
                const hcpp::u8 c2 = static_cast<hcpp::u8>(id >> 16);
                const hcpp::u8 c3 = static_cast<hcpp::u8>(id >> 24);
                if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9') continue;
                DemuxStreamType t = DemuxStreamType::Unknown;
                if ((c2 == 'd' && (c3 == 'c' || c3 == 'b')) ||
                    (c2 == 'D' && c3 == 'C'))
                    t = DemuxStreamType::Video;
                else if (c2 == 'w' && c3 == 'b')
                    t = DemuxStreamType::Audio;
                else
                    continue;
                const hcpp::u32 index = static_cast<hcpp::u32>(c0 - '0') * 10 + (c1 - '0');
                const DemuxStream* s = demux_stream_find(d, index);
                pkt.data = d.base + body;
                pkt.size = stop - body;
                pkt.stream_id = index;
                pkt.type = s != nullptr ? s->type : t;
                pkt.codec = s != nullptr ? s->codec : CodecType::Unknown;
                pkt.pts = 0;
                pkt.has_pts = false;
                return true;
            }
            return false;
        }
        hcpp::size_t ts_payload(const hcpp::u8* p, hcpp::size_t n,
                                hcpp::size_t i) noexcept {
            if (i + 188 > n || p[i] != 0x47) return n;
            const hcpp::u8 afc = (p[i + 3] >> 4) & 3;
            if (afc == 1) return i + 4;
            if (afc == 3 && i + 5 <= i + 188) {
                const hcpp::size_t off = i + 5 + p[i + 4];
                return off <= i + 188 ? off : n;
            }
            return n;
        }
        void ts_pat(const hcpp::u8* t, hcpp::size_t n, hcpp::u16& pmt) noexcept {
            if (n < 12 || t[0] != 0x00) return;
            const hcpp::size_t seclen =
                (static_cast<hcpp::size_t>(t[1] & 0x0F) << 8) | t[2];
            const hcpp::size_t tend = 3 + seclen <= n ? 3 + seclen - 4 : n;
            for (hcpp::size_t i = 8; i + 4 <= tend; i += 4) {
                const hcpp::u16 prog = static_cast<hcpp::u16>((t[i] << 8) | t[i + 1]);
                const hcpp::u16 pid =
                    static_cast<hcpp::u16>(((t[i + 2] & 0x1F) << 8) | t[i + 3]);
                if (prog != 0 && pmt == 0xFFFF) {
                    pmt = pid;
                    return;
                }
            }
        }
        CodecType ts_video_codec(hcpp::u8 st) noexcept {
            switch (st) {
                case 0x01:
                case 0x02:
                case 0x80:
                    return CodecType::Mpeg2;
                case 0x1B:
                case 0x24:
                case 0x10:
                    return CodecType::H264;
                default:
                    return CodecType::Unknown;
            }
        }
        bool ts_audio(hcpp::u8 st) noexcept {
            return st == 0x03 || st == 0x04 || st == 0x0F || st == 0x11 || st == 0x81 ||
                   st == 0x06;
        }
        void ts_pmt(Demuxer& d, const hcpp::u8* t, hcpp::size_t n) noexcept {
            if (n < 16 || t[0] != 0x02) return;
            const hcpp::size_t seclen =
                (static_cast<hcpp::size_t>(t[1] & 0x0F) << 8) | t[2];
            const hcpp::size_t pil =
                (static_cast<hcpp::size_t>(t[10] & 0x0F) << 8) | t[11];
            const hcpp::size_t tend = 3 + seclen <= n ? 3 + seclen - 4 : n;
            for (hcpp::size_t i = 12 + pil; i + 5 <= tend;) {
                const hcpp::u8 st = t[i];
                const hcpp::u16 pid =
                    static_cast<hcpp::u16>(((t[i + 1] & 0x1F) << 8) | t[i + 2]);
                const hcpp::size_t eslen =
                    (static_cast<hcpp::size_t>(t[i + 3] & 0x0F) << 8) | t[i + 4];
                const CodecType vc = ts_video_codec(st);
                if (vc != CodecType::Unknown)
                    add_stream(d, pid, DemuxStreamType::Video, vc, st, 0, 0);
                else if (ts_audio(st))
                    add_stream(d, pid, DemuxStreamType::Audio, CodecType::Unknown, st, 0,
                               0);
                i += 5 + eslen;
            }
        }
        bool ts_open(Demuxer& d) noexcept {
            hcpp::u16 pmt = 0xFFFF;
            for (hcpp::size_t i = 0; i + 188 <= d.size; i += 188) {
                if (d.base[i] != 0x47) continue;
                const hcpp::u16 pid =
                    static_cast<hcpp::u16>(((d.base[i + 1] & 0x1F) << 8) | d.base[i + 2]);
                const hcpp::size_t off = ts_payload(d.base, d.size, i);
                if (off == d.size || off >= i + 188) continue;
                const hcpp::u8* pay = d.base + off;
                const hcpp::size_t pn = i + 188 - off;
                if ((d.base[i + 1] & 0x40) == 0) continue;
                const hcpp::size_t ptr = pay[0];
                if (ptr + 1 >= pn) continue;
                if (pid == 0)
                    ts_pat(pay + 1 + ptr, pn - 1 - ptr, pmt);
                else if (pid == pmt) {
                    ts_pmt(d, pay + 1 + ptr, pn - 1 - ptr);
                    if (d.stream_count != 0) {
                        d.pos = 0;
                        d.data_end = d.size;
                        return true;
                    }
                }
            }
            return false;
        }
        bool ts_packet(Demuxer& d, DemuxPacket& pkt) noexcept {
            hcpp::size_t i = d.pos;
            while (i + 188 <= d.data_end) {
                if (d.base[i] != 0x47) {
                    ++i;
                    continue;
                }
                const hcpp::u16 pid =
                    static_cast<hcpp::u16>(((d.base[i + 1] & 0x1F) << 8) | d.base[i + 2]);
                const hcpp::size_t off = ts_payload(d.base, d.data_end, i);
                const hcpp::size_t end = i + 188;
                i += 188;
                if (off == d.data_end || off >= end || pid == 0x1FFF) continue;
                const DemuxStream* s = demux_stream_find(d, pid);
                if (s == nullptr) continue;
                const hcpp::u8* pay = d.base + off;
                const hcpp::size_t pn = end - off;
                pkt.data = pay;
                pkt.size = pn;
                pkt.stream_id = pid;
                pkt.type = s->type;
                pkt.codec = s->codec;
                pkt.pts = 0;
                pkt.has_pts = false;
                if ((d.base[i - 188 + 1] & 0x40) != 0 && pn >= 14 && pay[0] == 0 &&
                    pay[1] == 0 && pay[2] == 1 && (pay[7] & 0x80) != 0) {
                    const hcpp::u8* q = pay + 9;
                    pkt.pts = (static_cast<hcpp::u64>((q[0] >> 1) & 0x07) << 30) |
                              (static_cast<hcpp::u64>(q[1]) << 22) |
                              (static_cast<hcpp::u64>((q[2] >> 1) & 0x7F) << 15) |
                              (static_cast<hcpp::u64>(q[3]) << 7) |
                              static_cast<hcpp::u64>((q[4] >> 1) & 0x7F);
                    pkt.has_pts = true;
                }
                d.pos = i;
                return true;
            }
            d.pos = i;
            return false;
        }
        bool ebml_id(const hcpp::u8* b, hcpp::size_t n, hcpp::size_t& pos,
                     hcpp::u32& id) noexcept {
            if (pos >= n) return false;
            const hcpp::u8 f = b[pos];
            hcpp::u32 len = f & 0x80 ? 1 : f & 0x40 ? 2 : f & 0x20 ? 3 : f & 0x10 ? 4 : 0;
            if (len == 0 || pos + len > n) return false;
            id = 0;
            for (hcpp::u32 k = 0; k < len; ++k) id = (id << 8) | b[pos + k];
            pos += len;
            return true;
        }
        bool ebml_size(const hcpp::u8* b, hcpp::size_t n, hcpp::size_t& pos,
                       hcpp::u64& sz) noexcept {
            if (pos >= n) return false;
            const hcpp::u8 f = b[pos];
            hcpp::u32 len = 0;
            for (hcpp::u32 k = 0; k < 8; ++k)
                if (f & (0x80 >> k)) {
                    len = k + 1;
                    break;
                }
            if (len == 0 || pos + len > n) return false;
            sz = f & (0xFF >> len);
            for (hcpp::u32 k = 1; k < len; ++k) sz = (sz << 8) | b[pos + k];
            pos += len;
            const hcpp::u64 all = (1ULL << (len * 7)) - 1;
            if (sz == all) sz = ~0ULL;
            return true;
        }
        bool ebml_elem(const hcpp::u8* b, hcpp::size_t n, hcpp::size_t& pos,
                       hcpp::u32& id, hcpp::size_t& end) noexcept {
            if (!ebml_id(b, n, pos, id)) return false;
            hcpp::u64 sz = 0;
            if (!ebml_size(b, n, pos, sz)) return false;
            end = sz == ~0ULL || pos + sz > n ? n : pos + static_cast<hcpp::size_t>(sz);
            return true;
        }
        hcpp::u64 ebml_uint(const hcpp::u8* p, hcpp::size_t n) noexcept {
            hcpp::u64 v = 0;
            for (hcpp::size_t i = 0; i < n && i < 8; ++i) v = (v << 8) | p[i];
            return v;
        }
        void ebml_track(Demuxer& d, hcpp::size_t s, hcpp::size_t e) noexcept {
            hcpp::u32 num = 0;
            hcpp::u8 type = 0;
            CodecType codec = CodecType::Unknown;
            hcpp::u32 w = 0, h = 0;
            const hcpp::u8* priv = nullptr;
            hcpp::size_t priv_n = 0;
            const hcpp::u8* cid = nullptr;
            hcpp::size_t cid_n = 0;
            hcpp::size_t p = s;
            while (p < e) {
                hcpp::u32 id = 0;
                hcpp::size_t end = e;
                if (!ebml_elem(d.base, e, p, id, end)) break;
                const hcpp::size_t body = p;
                if (id == 0xD7)
                    num = static_cast<hcpp::u32>(ebml_uint(d.base + body, end - body));
                else if (id == 0x83)
                    type = static_cast<hcpp::u8>(ebml_uint(d.base + body, end - body));
                else if (id == 0x86) {
                    cid = d.base + body;
                    cid_n = end - body;
                } else if (id == 0x63A2) {
                    priv = d.base + body;
                    priv_n = end - body;
                } else if (id == 0xE0) {
                    hcpp::size_t q = body;
                    while (q < end) {
                        hcpp::u32 vid = 0;
                        hcpp::size_t vend = end;
                        if (!ebml_elem(d.base, end, q, vid, vend)) break;
                        if (vid == 0xB0)
                            w = static_cast<hcpp::u32>(
                                ebml_uint(d.base + q, vend - q));
                        else if (vid == 0xBA)
                            h = static_cast<hcpp::u32>(
                                ebml_uint(d.base + q, vend - q));
                        q = vend;
                    }
                }
                p = end;
            }
            if (cid != nullptr)
                codec = mkv_codec(cid, cid_n, priv, priv_n);
            if (num != 0 && type != 0)
                add_stream(d, num,
                           type == 1 ? DemuxStreamType::Video
                                     : type == 2 ? DemuxStreamType::Audio
                                                 : DemuxStreamType::Unknown,
                           codec, 0, w, h);
        }
        void ebml_tracks(Demuxer& d, hcpp::size_t s, hcpp::size_t e) noexcept {
            hcpp::size_t p = s;
            while (p < e) {
                hcpp::u32 id = 0;
                hcpp::size_t end = e;
                if (!ebml_elem(d.base, e, p, id, end)) break;
                if (id == 0xAE) ebml_track(d, p, end);
                p = end;
            }
        }
        void ebml_info(Demuxer& d, hcpp::size_t s, hcpp::size_t e) noexcept {
            hcpp::size_t p = s;
            while (p < e) {
                hcpp::u32 id = 0;
                hcpp::size_t end = e;
                if (!ebml_elem(d.base, e, p, id, end)) break;
                if (id == 0x2AD7B1)
                    d.timecode_scale = ebml_uint(d.base + p, end - p);
                p = end;
            }
        }
        bool ebml_open(Demuxer& d) noexcept {
            hcpp::size_t p = 0;
            hcpp::u32 id = 0;
            hcpp::size_t end = d.size;
            if (!ebml_elem(d.base, d.size, p, id, end) || id != 0x1A45DFA3)
                return false;
            p = end;
            while (p < d.size) {
                if (!ebml_elem(d.base, d.size, p, id, end)) return false;
                if (id != 0x18538067) {
                    p = end;
                    continue;
                }
                const hcpp::size_t seg_end = end;
                hcpp::size_t q = p;
                while (q < seg_end) {
                    hcpp::size_t eend = seg_end;
                    if (!ebml_elem(d.base, seg_end, q, id, eend)) break;
                    if (id == 0x1549A966) ebml_info(d, q, eend);
                    else if (id == 0x1654AE6B) ebml_tracks(d, q, eend);
                    else if (id == 0x1F43B675) {
                        d.pos = q;
                        d.data_end = seg_end;
                        d.cluster_end = eend;
                        return true;
                    }
                    q = eend;
                }
                return d.stream_count != 0;
            }
            return false;
        }
        bool ebml_block(Demuxer& d, hcpp::size_t s, hcpp::size_t e,
                        DemuxPacket& pkt) noexcept {
            hcpp::size_t p = s;
            if (p >= e) return false;
            hcpp::u64 track = 0;
            if (!ebml_size(d.base, e, p, track)) return false;
            if (p + 3 > e) return false;
            const hcpp::i16 tc = static_cast<hcpp::i16>(
                (static_cast<hcpp::u16>(d.base[p]) << 8) | d.base[p + 1]);
            const hcpp::size_t data = p + 3;
            const DemuxStream* st = demux_stream_find(d, static_cast<hcpp::u32>(track));
            const hcpp::i64 rel =
                static_cast<hcpp::i64>(d.cluster_timecode) + static_cast<hcpp::i64>(tc);
            pkt.data = d.base + data;
            pkt.size = e - data;
            pkt.stream_id = static_cast<hcpp::u32>(track);
            pkt.type = st != nullptr ? st->type : DemuxStreamType::Unknown;
            pkt.codec = st != nullptr ? st->codec : CodecType::Unknown;
            pkt.pts = rel > 0 ? static_cast<hcpp::u64>(rel) * d.timecode_scale : 0;
            pkt.has_pts = true;
            return true;
        }
        bool ebml_packet(Demuxer& d, DemuxPacket& pkt) noexcept {
            for (;;) {
                while (d.pos < d.cluster_end) {
                    hcpp::u32 id = 0;
                    hcpp::size_t end = d.cluster_end;
                    if (!ebml_elem(d.base, d.cluster_end, d.pos, id, end))
                        return false;
                    if (id == 0xA3) {
                        const hcpp::size_t body = d.pos;
                        d.pos = end;
                        if (ebml_block(d, body, end, pkt)) return true;
                    } else if (id == 0xA0) {
                        hcpp::size_t q = d.pos;
                        while (q < end) {
                            hcpp::u32 bid = 0;
                            hcpp::size_t bend = end;
                            if (!ebml_elem(d.base, end, q, bid, bend)) break;
                            if (bid == 0xA1 && ebml_block(d, q, bend, pkt)) {
                                d.pos = end;
                                return true;
                            }
                            q = bend;
                        }
                        d.pos = end;
                    } else {
                        if (id == 0xE7)
                            d.cluster_timecode =
                                ebml_uint(d.base + d.pos, end - d.pos);
                        d.pos = end;
                    }
                }
                hcpp::size_t q = d.pos < d.cluster_end ? d.cluster_end : d.pos;
                while (q < d.data_end) {
                    hcpp::u32 id = 0;
                    hcpp::size_t end = d.data_end;
                    if (!ebml_elem(d.base, d.data_end, q, id, end)) return false;
                    if (id == 0x1F43B675) {
                        d.pos = q;
                        d.cluster_end = end;
                        break;
                    }
                    q = end;
                }
                if (q >= d.data_end) {
                    d.pos = q;
                    return false;
                }
            }
        }
    }  // namespace
    hcpp::u32 fourcc(const char* t) noexcept {
        return static_cast<hcpp::u32>(static_cast<hcpp::u8>(t[0])) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(t[1])) << 8) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(t[2])) << 16) |
               (static_cast<hcpp::u32>(static_cast<hcpp::u8>(t[3])) << 24);
    }
    DemuxKind demux_probe(const hcpp::u8* b, hcpp::size_t n) noexcept {
        if (b == nullptr || n < 4) return DemuxKind::Unknown;
        if (n >= 12 && tag_eq(b, "RIFF") && tag_eq(b + 8, "AVI "))
            return DemuxKind::Avi;
        if (b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3)
            return DemuxKind::Ebml;
        if (n >= 188 && b[0] == 0x47) {
            if (n >= 376 && b[188] == 0x47 && b[376] == 0x47) return DemuxKind::MpegTs;
            if (n < 376 && n >= 188) return DemuxKind::MpegTs;
        }
        return DemuxKind::Unknown;
    }
    const char* demux_kind_name(DemuxKind k) noexcept {
        switch (k) {
            case DemuxKind::Avi: return "avi";
            case DemuxKind::MpegTs: return "ts";
            case DemuxKind::Ebml: return "ebml";
            default: return "unknown";
        }
    }
    bool demux_open(Demuxer& d, const hcpp::u8* b, hcpp::size_t n) noexcept {
        d = {};
        d.base = b;
        d.size = n;
        d.kind = demux_probe(b, n);
        d.video_stream_id = ~0u;
        d.audio_stream_id = ~0u;
        d.timecode_scale = 1000000;
        if (d.kind == DemuxKind::Avi) return avi_open(d);
        if (d.kind == DemuxKind::MpegTs) return ts_open(d);
        if (d.kind == DemuxKind::Ebml) return ebml_open(d);
        return false;
    }
    bool demux_packet(Demuxer& d, DemuxPacket& pkt) noexcept {
        pkt = {};
        if (d.kind == DemuxKind::Avi) return avi_packet(d, pkt);
        if (d.kind == DemuxKind::MpegTs) return ts_packet(d, pkt);
        if (d.kind == DemuxKind::Ebml) return ebml_packet(d, pkt);
        return false;
    }
    hcpp::size_t demux_stream_count(const Demuxer& d) noexcept {
        return d.stream_count;
    }
    const DemuxStream* demux_stream_at(const Demuxer& d, hcpp::size_t i) noexcept {
        return i < d.stream_count ? &d.streams[i] : nullptr;
    }
    const DemuxStream* demux_stream_find(const Demuxer& d, hcpp::u32 id) noexcept {
        for (hcpp::size_t i = 0; i < d.stream_count; ++i)
            if (d.streams[i].id == id) return &d.streams[i];
        return nullptr;
    }
    bool emuxer_probe_packet(const StreamPacket& p) noexcept {
        return p.data != nullptr && p.size != 0;
    }
    CodecType emuxer_codec(const StreamPacket& p) noexcept {
        if (p.data == nullptr || p.size < 4) return CodecType::Unknown;
        if (mp2_packet_start(p.data, p.size)) return CodecType::Mpeg2;
        if (mjpeg_probe(p.data, p.size)) return CodecType::Mjpeg;
        if (emuxer_avi_probe(p.data, p.size)) return CodecType::Mjpeg;
        return CodecType::Unknown;
    }
    bool emuxer_avi_probe(const hcpp::u8* src, hcpp::size_t n) noexcept {
        if (src == nullptr || n < 12) return false;
        if (!tag_eq(src, "RIFF") || !tag_eq(src + 8, "AVI ")) return false;
        const hcpp::size_t scan = n < 2048 ? n : 2048;
        for (hcpp::size_t i = 12; i + 4 <= scan; ++i)
            if (rd32(src + i) == fourcc("MJPG")) return true;
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
            if (id == fourcc("LIST") && body + 4 <= n &&
                rd32(src + body) == fourcc("movi")) {
                hcpp::size_t j = body + 4;
                while (j + 8 <= end) {
                    const hcpp::u32 cid = rd32(src + j);
                    const hcpp::u32 csz = rd32(src + j + 4);
                    if ((cid == fourcc("00dc") || cid == fourcc("00db")) && csz != 0 &&
                        j + 8 + csz <= n && mjpeg_probe(src + j + 8, csz)) {
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
