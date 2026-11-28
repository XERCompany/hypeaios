// AMDTP / AM824 codec + stream lifecycle + iso-pipe glue + TASCAM
// capture/playback paths.
//
// The codec is fully real: amdtp_encode() emits a standards-shaped
// two-quadlet CIP header + MBLA24 data blocks, amdtp_decode() validates
// and unpacks the same with dbc sequencing; amdtp_codec_selftest()
// round-trips a known PCM buffer end to end. The stream/pipe layers are
// the lifecycle skeleton: they size and bind the OHCI iso contexts
// programmed in firewirebus.cc and deliver received payloads into the
// capture ring.

#include "kernel/firewire/amdtpam824.h"

#include "hcpp/memory.hpp"
#include "kernel/firewire/amdtpstream.h"
#include "kernel/firewire/amdtpstreampipe.h"
#include "kernel/firewire/firewirebus.h"
#include "kernel/firewire/iso-resources.h"
#include "kernel/firewire/tascam_devkey.h"
#include "kernel/firewire/tascam_pcm.h"
#include "kernel/firewire/tascam_record.h"
#include "kernel/serial.hpp"

namespace hype::fw {
    namespace {

        // -----------------------------------------------------------
        // CIP / data-block primitives
        // -----------------------------------------------------------

        constexpr hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
            return (hcpp::u32(p[0]) << 24) | (hcpp::u32(p[1]) << 16) |
                   (hcpp::u32(p[2]) << 8) | hcpp::u32(p[3]);
        }
        void write_be32(hcpp::u8 *p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v >> 24);
            p[1] = static_cast<hcpp::u8>(v >> 16);
            p[2] = static_cast<hcpp::u8>(v >> 8);
            p[3] = static_cast<hcpp::u8>(v);
        }

        hcpp::i32 quadlet_to_mbla(hcpp::u32 q) noexcept {
            // Low 24 bits are a big-endian two's-complement sample;
            // sign-extend into i32.
            hcpp::i32 s = static_cast<hcpp::i32>(q & 0x00FFFFFFu);
            if ((s & 0x00800000) != 0) s -= 0x01000000;
            return s;
        }

        hcpp::u32 mbla_to_quadlet(hcpp::i32 s) noexcept {
            // Clamp to signed 24-bit; label byte = MBLA24.
            if (s > 0x007FFFFF) s = 0x007FFFFF;
            if (s < -0x00800000) s = -0x00800000;
            return (hcpp::u32(amdtp_label_mbla24) << 24) |
                   (static_cast<hcpp::u32>(s) & 0x00FFFFFFu);
        }

        // -----------------------------------------------------------
        // Stream + pipe + capture state
        // -----------------------------------------------------------

        FwIsoPipe pipes[fw_iso_pipe_max];
        hcpp::u32 pipe_count;

        TascamCapture capture;
        bool playback_running;

        void capture_packet_cb(const hcpp::u8 *payload, hcpp::u32 bytes,
                               void *ctx) noexcept;

    }  // namespace

    // -----------------------------------------------------------
    // Codec
    // -----------------------------------------------------------

    hcpp::u8 amdtp_sfc_for_rate(hcpp::u32 rate) noexcept {
        switch (rate) {
        case 32000: return sfc_32k;
        case 44100: return sfc_44k1;
        case 48000: return sfc_48k;
        case 88200: return sfc_88k2;
        case 96000: return sfc_96k;
        case 176400: return sfc_176k4;
        case 192000: return sfc_192k;
        default: return 0xFF;
        }
    }

    hcpp::u32 amdtp_rate_for_sfc(hcpp::u8 sfc) noexcept {
        static const hcpp::u32 rates[sfc_count] = {
            32000, 44100, 48000, 88200, 96000, 176400, 192000};
        return sfc < sfc_count ? rates[sfc] : 0;
    }

    hcpp::u32 amdtp_nominal_blocks(hcpp::u32 rate) noexcept {
        return (rate + 4000) / 8000;  // 8000 iso cycles per second
    }

    void amdtp_codec_init(AmdtpCodec &codec, hcpp::u8 channels,
                          hcpp::u8 midi_quadlets, hcpp::u32 rate,
                          hcpp::u8 sid) noexcept {
        codec = {};
        codec.channels = channels;
        codec.midi_quadlets =
            midi_quadlets > 8 ? 8 : midi_quadlets;
        codec.dbs = static_cast<hcpp::u8>(channels + codec.midi_quadlets);
        if (codec.dbs > amdtp_max_dbs) codec.dbs = amdtp_max_dbs;
        const hcpp::u8 sfc = amdtp_sfc_for_rate(rate);
        codec.sfc = sfc == 0xFF ? sfc_48k : sfc;
        codec.rate = sfc == 0xFF ? 48000 : rate;
        codec.sid = sid & 0x3F;
        codec.tx_dbc = 0;
        codec.rx_dbc_valid = false;
    }

    bool amdtp_parse_cip(const hcpp::u8 *packet, hcpp::u32 len,
                         AmdtpCip &cip) noexcept {
        if (packet == nullptr || len < amdtp_header_bytes) return false;
        const hcpp::u32 q0 = read_be32(packet);
        const hcpp::u32 q1 = read_be32(packet + 4);
        // Two-quadlet header: eoh = 0 in q0, 1 in q1.
        if ((q0 & cip_eoh) != 0 || (q1 & cip_eoh) == 0) return false;
        if ((q1 & cip_fmt_mask) != (hcpp::u32(cip_fmt_am824) << 24)) {
            return false;
        }
        cip.sid = static_cast<hcpp::u8>((q0 & cip_sid_mask) >> 24);
        cip.dbs = static_cast<hcpp::u8>((q0 & cip_dbs_mask) >> 16);
        cip.fn = static_cast<hcpp::u8>((q0 & cip_fn_mask) >> 14);
        cip.qpc = static_cast<hcpp::u8>((q0 & cip_qpc_mask) >> 11);
        cip.sph = (q0 & cip_sph) != 0;
        cip.dbc = static_cast<hcpp::u8>(q0 & cip_dbc_mask);
        cip.fmt = static_cast<hcpp::u8>((q1 & cip_fmt_mask) >> 24);
        cip.fdf = static_cast<hcpp::u8>((q1 & cip_fdf_mask) >> 16);
        cip.syt = static_cast<hcpp::u16>(q1 & cip_syt_mask);
        return true;
    }

    hcpp::u32 amdtp_encode(AmdtpCodec &codec, const hcpp::i32 *pcm24,
                           hcpp::u32 data_blocks, hcpp::u8 *out,
                           hcpp::u32 out_cap) noexcept {
        if (out == nullptr || codec.dbs == 0) return 0;
        const hcpp::u32 need =
            amdtp_header_bytes + codec.dbs * 4 * data_blocks;
        if (out_cap < need) return 0;

        // CIP quadlet 0: sid | dbs | fn=0 | qpc=0 | sph=0 | dbc.
        // dbc counts data blocks; a NO_DATA packet carries dbc = the
        // running counter unchanged.
        write_be32(out,
                   (hcpp::u32(codec.sid) << 24) |
                       (hcpp::u32(codec.dbs) << 16) |
                       hcpp::u32(codec.tx_dbc));
        const hcpp::u8 fdf =
            data_blocks == 0
                ? amdtp_fdf_no_data
                : static_cast<hcpp::u8>(codec.sfc);
        write_be32(out + 4,
                   cip_eoh | (hcpp::u32(cip_fmt_am824) << 24) |
                       (hcpp::u32(fdf) << 16) | cip_syt_no_info);

        hcpp::u8 *block = out + amdtp_header_bytes;
        for (hcpp::u32 b = 0; b < data_blocks; ++b) {
            for (hcpp::u32 ch = 0; ch < codec.channels; ++ch) {
                const hcpp::i32 s =
                    pcm24 != nullptr ? pcm24[b * codec.channels + ch] : 0;
                write_be32(block + (b * codec.dbs + ch) * 4,
                           mbla_to_quadlet(s));
            }
            for (hcpp::u32 m = 0; m < codec.midi_quadlets; ++m) {
                write_be32(
                    block + (b * codec.dbs + codec.channels + m) * 4,
                    hcpp::u32(amdtp_label_midi) << 24);
            }
        }
        codec.tx_dbc = static_cast<hcpp::u8>(codec.tx_dbc + data_blocks);
        ++codec.tx_packets;
        return need;
    }

    hcpp::u32 amdtp_decode(AmdtpCodec &codec, const hcpp::u8 *packet,
                           hcpp::u32 len, hcpp::i32 *pcm24,
                           hcpp::u32 max_frames) noexcept {
        AmdtpCip cip;
        if (!amdtp_parse_cip(packet, len, cip)) return 0;
        ++codec.rx_packets;
        if (cip.fdf == amdtp_fdf_no_data || cip.dbs == 0) {
            ++codec.rx_no_data;
            return 0;
        }
        const hcpp::u32 payload_quads = (len - amdtp_header_bytes) / 4;
        hcpp::u32 blocks = payload_quads / cip.dbs;
        if (blocks > max_frames) blocks = max_frames;

        // dbc sequencing: expected = previous + blocks. A discontinuity
        // flags a dropped packet (counted, then resynced).
        if (codec.rx_dbc_valid && codec.rx_dbc_expected != cip.dbc) {
            ++codec.rx_seq_errors;
        }
        codec.rx_dbc_expected =
            static_cast<hcpp::u8>(cip.dbc + blocks);
        codec.rx_dbc_valid = true;

        const hcpp::u8 audio = codec.channels < cip.dbs
                                   ? codec.channels
                                   : cip.dbs;
        for (hcpp::u32 b = 0; b < blocks; ++b) {
            for (hcpp::u32 ch = 0; ch < audio; ++ch) {
                const hcpp::u32 q =
                    read_be32(packet + amdtp_header_bytes +
                              (b * cip.dbs + ch) * 4);
                if ((q >> 24) != amdtp_label_mbla24) {
                    ++codec.rx_dropped_labels;
                    if (pcm24 != nullptr) {
                        pcm24[b * codec.channels + ch] = 0;
                    }
                    continue;
                }
                if (pcm24 != nullptr) {
                    pcm24[b * codec.channels + ch] = quadlet_to_mbla(q);
                }
            }
        }
        return blocks;
    }

    bool amdtp_codec_selftest() noexcept {
        AmdtpCodec tx, rx;
        amdtp_codec_init(tx, 2, 0, 48000, 7);
        amdtp_codec_init(rx, 2, 0, 48000, 0);

        // Build a deterministic stereo ramp/pattern across two packets
        // (6 blocks per packet at 48 kHz).
        hcpp::i32 pcm[2 * 12];
        for (hcpp::u32 i = 0; i < 24; ++i) {
            pcm[i] = static_cast<hcpp::i32>(i) * 65536 -
                     0x200000 + static_cast<hcpp::i32>((i * 37) & 0xFF);
        }
        hcpp::u8 packet[8 + 2 * 4 * 6];
        hcpp::i32 decoded[2 * 12];
        for (hcpp::u32 p = 0; p < 2; ++p) {
            const hcpp::u32 len =
                amdtp_encode(tx, pcm + p * 12, 6, packet, sizeof(packet));
            if (len != 8 + 48) return false;
            // Verify header shape directly.
            AmdtpCip cip;
            if (!amdtp_parse_cip(packet, len, cip) || cip.dbs != 2 ||
                cip.fmt != cip_fmt_am824 || cip.fdf != sfc_48k ||
                cip.dbc != static_cast<hcpp::u8>(p * 6)) {
                return false;
            }
            const hcpp::u32 blocks =
                amdtp_decode(rx, packet, len, decoded + p * 12, 6);
            if (blocks != 6) return false;
        }
        for (hcpp::u32 i = 0; i < 24; ++i) {
            if (decoded[i] != pcm[i]) return false;
        }
        if (rx.rx_seq_errors != 0 || rx.rx_packets != 2) return false;

        // Drop-check: patch the dbc byte so it does not match the
        // expected running counter — the decoder must flag and resync.
        amdtp_encode(tx, pcm, 6, packet, sizeof(packet));
        packet[3] += 2;
        if (amdtp_decode(rx, packet, sizeof(packet), decoded, 6) != 6 ||
            rx.rx_seq_errors == 0) {
            return false;
        }

        // Wide-block check: 18-channel FW-1884-shaped block, silence.
        AmdtpCodec wide;
        amdtp_codec_init(wide, 18, 4, 48000, 0);
        hcpp::u8 big[8 + 22 * 4 * 6];
        const hcpp::u32 big_len =
            amdtp_encode(wide, nullptr, 6, big, sizeof(big));
        return big_len == 8 + 22 * 4 * 6 && wide.dbs == 22;
    }

    // -----------------------------------------------------------
    // Stream lifecycle (amdtpstream.h)
    // -----------------------------------------------------------

    bool amdtp_stream_open(AmdtpStream &stream, AmdtpDir dir,
                           hcpp::u8 channels, hcpp::u8 midi_quadlets,
                           hcpp::u32 rate, hcpp::u8 sid) noexcept {
        stream = {};
        amdtp_codec_init(stream.codec, channels, midi_quadlets, rate, sid);
        stream.used = true;
        stream.dir = dir;
        stream.channel = 0xFF;
        stream.ctx_index = fw_no_iso_ctx;
        stream.node = broadcast_node;
        stream.speed = speed_s400;
        stream.packets_per_buffer = 8;
        stream.packet_payload_bytes =
            amdtp_header_bytes +
            stream.codec.dbs * 4 * amdtp_nominal_blocks(rate);
        return true;
    }

    bool amdtp_stream_start(AmdtpStream &stream, hcpp::u8 channel,
                            hcpp::u8 ctx_index, hcpp::u8 node) noexcept {
        if (!stream.used || channel > 63) return false;
        stream.channel = channel;
        stream.ctx_index = ctx_index;
        stream.node = node;
        stream.running = true;
        return true;
    }

    void amdtp_stream_stop(AmdtpStream &stream) noexcept {
        stream.running = false;
    }

    bool amdtp_stream_running(const AmdtpStream &stream) noexcept {
        return stream.running;
    }

    hcpp::u32 amdtp_stream_packets(const AmdtpStream &stream) noexcept {
        return stream.packets_seen;
    }

    void amdtp_stream_deliver(AmdtpStream &stream, const hcpp::u8 *payload,
                              hcpp::u32 bytes) noexcept {
        if (!stream.running) return;
        ++stream.packets_seen;
        if (stream.packet_fn != nullptr) {
            stream.packet_fn(payload, bytes, stream.packet_ctx);
        }
    }

    // -----------------------------------------------------------
    // Iso pipe (amdtpstreampipe.h)
    // -----------------------------------------------------------

    FwIsoPipe *fw_iso_pipe_open(AmdtpStream &stream, hcpp::u8 channel,
                                hcpp::u8 node) noexcept {
        for (hcpp::u32 i = 0; i < fw_iso_pipe_max; ++i) {
            if (pipes[i].used) continue;
            FwIsoPipe &p = pipes[i];
            p = {};
            p.used = true;
            p.rx = stream.dir == AmdtpDir::Rx;
            p.channel = channel;
            p.ctx_index = fw_no_iso_ctx;
            p.stream = &stream;
            if (p.rx) {
                stream.packet_fn = capture_packet_cb;
                stream.packet_ctx = &stream;
                const hcpp::u8 ctx =
                    fw_iso_rx_attach(channel,
                                     [](const hcpp::u8 *pkt, hcpp::u32 len,
                                        void *c) noexcept {
                                         auto *s = static_cast<AmdtpStream *>(
                                             c);
                                         amdtp_stream_deliver(*s, pkt, len);
                                     },
                                     &stream);
                if (ctx == fw_no_iso_ctx) {
                    p.used = false;
                    return nullptr;
                }
                p.ctx_index = ctx;
            }
            if (!amdtp_stream_start(stream, channel, p.ctx_index, node)) {
                if (p.rx) fw_iso_rx_detach(p.ctx_index);
                p.used = false;
                return nullptr;
            }
            ++pipe_count;
            return &p;
        }
        return nullptr;
    }

    void fw_iso_pipe_close(FwIsoPipe *pipe) noexcept {
        if (pipe == nullptr || !pipe->used) return;
        if (pipe->rx && pipe->ctx_index != fw_no_iso_ctx) {
            fw_iso_rx_detach(pipe->ctx_index);
        }
        if (pipe->stream != nullptr) {
            amdtp_stream_stop(*pipe->stream);
        }
        if (pipe->channel <= 63) {
            fw_irm_free_channel(pipe->channel);
        }
        *pipe = {};
        if (pipe_count != 0) --pipe_count;
    }

    hcpp::u32 fw_iso_pipe_count() noexcept { return pipe_count; }

    // -----------------------------------------------------------
    // TASCAM record path (tascam_record.h)
    // -----------------------------------------------------------

    namespace {
        void capture_packet_cb(const hcpp::u8 *payload, hcpp::u32 bytes,
                               void *ctx) noexcept {
            auto *stream = static_cast<AmdtpStream *>(ctx);
            if (!capture.running) return;
            static hcpp::i32 block_pcm[64 * amdtp_max_dbs];
            const hcpp::u32 frames =
                amdtp_decode(stream->codec, payload, bytes, block_pcm, 64);
            if (frames != 0) {
                tascam_capture_block(block_pcm, frames,
                                     stream->codec.channels);
            }
        }
    }

    bool tascam_capture_start(const TascamDevKey *dev, hcpp::u8 node,
                              hcpp::u32 rate) noexcept {
        if (dev == nullptr) return false;
        capture.running = true;
        capture.dev = dev;
        capture.node = node;
        capture.rate = rate;
        capture.channels = dev->capture_channels;
        capture.head = 0;
        capture.tail = 0;
        capture.underruns = 0;
        capture.overflow_frames = 0;
        capture.frames_captured = 0;
        hcpp::set(capture.ring, 0, sizeof(capture.ring));
        return true;
    }

    void tascam_capture_stop() noexcept { capture.running = false; }
    bool tascam_capture_running() noexcept { return capture.running; }
    TascamCapture *tascam_capture() noexcept { return &capture; }

    void tascam_capture_block(const hcpp::i32 *pcm24, hcpp::u32 frames,
                              hcpp::u8 channels) noexcept {
        if (!capture.running || pcm24 == nullptr || channels == 0) return;
        for (hcpp::u32 f = 0; f < frames; ++f) {
            if (capture.tail - capture.head >= tascam_ring_frames) {
                // Ring full: drop oldest frame and count it.
                ++capture.head;
                capture.overflow_frames += 1;
            }
            const hcpp::i32 l = pcm24[f * channels];
            const hcpp::i32 r =
                channels > 1 ? pcm24[f * channels + 1] : l;
            hcpp::i16 *slot =
                capture.ring +
                (capture.tail % tascam_ring_frames) * 2;
            slot[0] = static_cast<hcpp::i16>(l >> 8);
            slot[1] = static_cast<hcpp::i16>(r >> 8);
            ++capture.tail;
        }
        capture.frames_captured += frames;
    }

    hcpp::u32 tascam_capture_read(hcpp::i16 *dst, hcpp::u32 frames) noexcept {
        if (dst == nullptr) return 0;
        hcpp::u32 produced = 0;
        for (hcpp::u32 f = 0; f < frames; ++f) {
            if (capture.tail - capture.head == 0) {
                ++capture.underruns;
                dst[f * 2] = 0;
                dst[f * 2 + 1] = 0;
                continue;
            }
            const hcpp::i16 *slot =
                capture.ring +
                (capture.head % tascam_ring_frames) * 2;
            dst[f * 2] = slot[0];
            dst[f * 2 + 1] = slot[1];
            ++capture.head;
            ++produced;
        }
        return produced;
    }

    // -----------------------------------------------------------
    // TASCAM playback path (tascam_pcm.h)
    // -----------------------------------------------------------

    bool tascam_pcm_params(const TascamDevKey &dev, hcpp::u32 rate,
                           hcpp::u8 channels, TascamPcmParams &out) noexcept {
        const hcpp::u8 sfc = amdtp_sfc_for_rate(rate);
        if (sfc == 0xFF || channels == 0) return false;
        out = {};
        out.rate = rate;
        out.sfc = sfc;
        out.channels = channels > dev.playback_channels
                           ? dev.playback_channels
                           : channels;
        out.midi_quadlets = dev.midi_ports;
        out.dbs = static_cast<hcpp::u8>(out.channels + out.midi_quadlets);
        out.blocks_per_packet = amdtp_nominal_blocks(rate);
        out.packet_payload_bytes =
            amdtp_header_bytes + out.dbs * 4 * out.blocks_per_packet;
        return true;
    }

    bool tascam_playback_start(const TascamDevKey *dev, hcpp::u8 /*node*/,
                               hcpp::u32 rate, hcpp::u8 channels) noexcept {
        if (dev == nullptr) return false;
        TascamPcmParams p;
        if (!tascam_pcm_params(*dev, rate, channels, p)) return false;
        playback_running = true;
        return true;
    }

    void tascam_playback_stop() noexcept { playback_running = false; }
    bool tascam_playback_running() noexcept { return playback_running; }

}  // namespace hype::fw
