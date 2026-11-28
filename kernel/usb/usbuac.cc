// USB Audio Class 1.0 (UAC1) output driver.
//
// Enumeration: xhci.cc's scan_config() detects the AudioControl
// (01/01) + AudioStreaming (01/02) interface pair with an isoch-OUT
// endpoint and calls uac_bind() with the raw config descriptor still
// in the shared control scratch page. This file copies the blob to a
// private page first (later host_control() calls reuse the scratch),
// then walks it for:
//   * the AC feature unit (unit id + volume capability) for the
//     SET_CUR volume poke;
//   * every non-zero AS alt setting's class-specific descriptors —
//     AS_GENERAL (wFormatTag), FORMAT_TYPE (PCM channels/subframe/
//     bits/discrete frequencies) and the isoch-OUT endpoint
//     (wMaxPacketSize + bInterval).
// The best alt wins (16-bit PCM, preferring stereo @48 kHz whose
// worst-case 1 ms packet fits wMaxPacketSize); it is selected with
// SET_INTERFACE and the xHCI side arms the endpoint via
// host_iso_arm().
//
// Streaming proof: one isoch TD per 1 ms USB frame, up to 16 frames
// of a continuous 440 Hz sine rendered at the negotiated rate by
// audio::waveform_render_sine (which also proves the PCM payload is
// real data, not a filler pattern). Per-frame byte counts follow the
// cumulative rate*i/1000 schedule so fractional rates like 44.1 kHz
// get their 44/45-sample alternation right. Every TRB carries SIA +
// IOC; completions come back through the polled event ring into
// uac_iso_done().
//
// Scope (deliberate): 16-bit PCM only, first usable alt, one output
// device, no feedback/sync endpoint, no capture (isoch-IN), no
// continuous re-priming after the proof burst — that is a real
// streaming pipe, not yet a pullmix sink.
//
// Markers documented in usbuac.h.

#include "kernel/usb/usbuac.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/audio/waveform.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/usb/xhci.h"

namespace hype::usb::usbuac {

    namespace {

        constexpr hcpp::u8 code_success = 1;
        // Benign end-of-burst event: after the last queued TD the xHC
        // reports the isoch ring running dry — it is not a TD error.
        constexpr hcpp::u8 code_ring_underrun = 14;

        // USB descriptor types / audio class constants.
        constexpr hcpp::u8 desc_interface = 4;
        constexpr hcpp::u8 desc_endpoint = 5;
        constexpr hcpp::u8 desc_cs_interface = 0x24;
        constexpr hcpp::u8 audio_class = 1;
        constexpr hcpp::u8 audio_sub_control = 1;   // AudioControl
        constexpr hcpp::u8 audio_sub_streaming = 2; // AudioStreaming
        constexpr hcpp::u8 cs_as_general = 1;
        constexpr hcpp::u8 cs_format_type = 2;
        constexpr hcpp::u8 cs_feature_unit = 6;
        constexpr hcpp::u16 format_tag_pcm = 0x0001;
        constexpr hcpp::u8 fmt_type1_pcm = 1;

        // Audio class requests.
        constexpr hcpp::u8 req_set_cur = 0x01;
        constexpr hcpp::u8 req_get_min = 0x82;
        constexpr hcpp::u8 req_get_max = 0x83;
        constexpr hcpp::u8 cs_volume = 0x02;  // volume control selector

        constexpr hcpp::u32 max_alts = 8;
        constexpr hcpp::u32 max_freqs = 8;
        constexpr hcpp::u32 iso_frames_max = 16;  // 16 x 1 ms proof burst
        constexpr hcpp::u64 wait_iso_ticks = 400;  // PIT @100 Hz -> ~4 s
        constexpr hcpp::u32 fs_iso_max_packet = 1023;

        struct AsAlt {
            bool has_general;
            hcpp::u16 format_tag;
            bool has_format;
            hcpp::u8 fmt_type;
            hcpp::u8 channels;
            hcpp::u8 subframe;
            hcpp::u8 bits;
            hcpp::u32 freqs[max_freqs];
            hcpp::u8 freq_count;
            hcpp::u8 iface;
            hcpp::u8 alt;
            hcpp::u8 ep_num;
            hcpp::u16 ep_mps;
            hcpp::u8 ep_interval;
        };

        struct AcInfo {
            bool seen;
            hcpp::u8 iface;
            bool fu_seen;
            hcpp::u8 fu_id;
            // First channel whose bmaControls entry has the volume bit;
            // 0xFF when the feature unit has no volume control (master
            // channel 0 often only carries mute — QEMU does that and
            // puts volume on channels 1..N).
            hcpp::u8 fu_volume_ch = 0xFF;
        };

        AsAlt alts[max_alts];
        hcpp::u32 alt_count;
        AcInfo ac;

        // Streaming state for the in-flight isoch burst.
        HostDevice *active_dev;
        hcpp::u32 iso_done;
        hcpp::u32 iso_errors;
        hcpp::u8 iso_last_code;

        bool seen;        // any UAC device bound at all (markers)
        bool bound;       // one audio output at a time
        bool stream_ok;   // last burst completed cleanly

        hcpp::u64 desc_copy_phys;

        volatile hcpp::u8 *scratch() noexcept {
            return reinterpret_cast<volatile hcpp::u8 *>(host_scratch());
        }

        hcpp::u32 le24(const volatile hcpp::u8 *p) noexcept {
            return hcpp::u32(p[0]) | (hcpp::u32(p[1]) << 8) |
                   (hcpp::u32(p[2]) << 16);
        }

        void uac_fail(hcpp::u8 step) noexcept {
            serial::write("uac dev=fail s=");
            serial::write_dec32(step);
            serial::write("\r\n");
        }

        // Walk the copied config descriptor. Each AS interface alt
        // setting != 0 gets an AsAlt record that later class-specific
        // and endpoint descriptors fill in; `cur` tracks the record
        // the trailing descriptors belong to.
        void parse_config(const volatile hcpp::u8 *d,
                          hcpp::u32 len) noexcept {
            alt_count = 0;
            ac = AcInfo{};
            if (len < 9 || d[1] != 2) return;
            hcpp::u8 cls = 0;
            hcpp::u8 sub = 0;
            AsAlt *cur = nullptr;
            hcpp::u32 i = d[0] == 0 ? 9 : d[0];
            while (i + 1 < len) {
                const hcpp::u8 blen = d[i];
                const hcpp::u8 btype = d[i + 1];
                if (blen < 2) break;
                if (btype == desc_interface && i + 8 < len) {
                    cls = d[i + 5];
                    sub = d[i + 6];
                    cur = nullptr;
                    if (cls == audio_class && sub == audio_sub_control &&
                        !ac.seen) {
                        ac.seen = true;
                        ac.iface = d[i + 2];
                    }
                    if (cls == audio_class &&
                        sub == audio_sub_streaming && d[i + 3] != 0 &&
                        alt_count < max_alts) {
                        cur = &alts[alt_count++];
                        *cur = AsAlt{};
                        cur->iface = d[i + 2];
                        cur->alt = d[i + 3];
                    }
                } else if (btype == desc_cs_interface && i + 2 < len) {
                    const hcpp::u8 subtype = d[i + 2];
                    if (cls == audio_class && sub == audio_sub_control &&
                        subtype == cs_feature_unit && blen >= 7 &&
                        !ac.fu_seen) {
                        ac.fu_seen = true;
                        ac.fu_id = d[i + 3];
                        // bControlSize bytes per bmaControls entry, one
                        // entry per channel starting at the master
                        // (channel 0). bit0 mute, bit1 volume.
                        const hcpp::u8 bcs = d[i + 5];
                        for (hcpp::u8 c = 0; c < 16 && bcs != 0; ++c) {
                            const hcpp::u32 at = 6 + hcpp::u32(c) * bcs;
                            if (at >= blen) break;
                            // Control bitmaps are little-endian; bit1
                            // (volume) always sits in the low byte.
                            if ((d[i + at] & 0x02) != 0) {
                                ac.fu_volume_ch = c;
                                break;
                            }
                        }
                    } else if (cls == audio_class &&
                               sub == audio_sub_streaming &&
                               cur != nullptr) {
                        if (subtype == cs_as_general && blen >= 7) {
                            cur->has_general = true;
                            cur->format_tag = static_cast<hcpp::u16>(
                                d[i + 5] | (hcpp::u16(d[i + 6]) << 8));
                        } else if (subtype == cs_format_type &&
                                   blen >= 8) {
                            cur->has_format = true;
                            cur->fmt_type = d[i + 3];
                            cur->channels = d[i + 4];
                            cur->subframe = d[i + 5];
                            cur->bits = d[i + 6];
                            const hcpp::u8 nfreq = d[i + 7];
                            cur->freq_count = 0;
                            if (nfreq == 0) {
                                // Continuous range: tLowerSamFreq at
                                // byte 8; use it as the base rate.
                                if (blen >= 14) {
                                    cur->freqs[0] = le24(d + i + 8);
                                    cur->freq_count = 1;
                                }
                            } else {
                                for (hcpp::u8 f = 0; f < nfreq &&
                                                    f < max_freqs &&
                                                    8 + 3 * f + 2 < blen;
                                     ++f) {
                                    cur->freqs[f] = le24(d + i + 8 + 3 * f);
                                    ++cur->freq_count;
                                }
                            }
                        }
                    }
                } else if (btype == desc_endpoint && i + 6 < len) {
                    const hcpp::u8 addr = d[i + 2];
                    const hcpp::u8 attr = d[i + 3] & 0x03;
                    const hcpp::u16 mps = static_cast<hcpp::u16>(
                        (d[i + 4] | (hcpp::u16(d[i + 5]) << 8)) & 0x7FF);
                    if (cls == audio_class && sub == audio_sub_streaming &&
                        cur != nullptr && attr == 1 &&
                        (addr & 0x80) == 0 && cur->ep_num == 0) {
                        cur->ep_num = static_cast<hcpp::u8>(addr & 0x0F);
                        cur->ep_mps = mps;
                        cur->ep_interval = d[i + 6];
                    }
                }
                i = static_cast<hcpp::u32>(i + blen);
            }
        }

        // Worst-case bytes one 1 ms frame needs at `rate` for this alt.
        hcpp::u32 frame_bytes_max(const AsAlt &a,
                                  hcpp::u32 rate) noexcept {
            const hcpp::u32 samples = (rate + 999) / 1000;
            return samples * a.channels * a.subframe;
        }

        // Pick the alt's rate: prefer 48000, then 44100, then the
        // highest advertised rate whose worst-case packet fits.
        hcpp::u32 pick_rate(const AsAlt &a) noexcept {
            hcpp::u32 fallback = 0;
            for (hcpp::u8 f = 0; f < a.freq_count; ++f) {
                const hcpp::u32 r = a.freqs[f];
                if (r < 1000 || frame_bytes_max(a, r) > a.ep_mps ||
                    frame_bytes_max(a, r) > fs_iso_max_packet) {
                    continue;
                }
                if (r == 48000) return r;
                if (r == 44100) {
                    fallback = r;
                    continue;
                }
                if (r > fallback && fallback != 44100) fallback = r;
            }
            return fallback;
        }

        const AsAlt *pick_alt(hcpp::u32 &rate_out) noexcept {
            const AsAlt *best = nullptr;
            hcpp::u32 best_rate = 0;
            int best_score = -1;
            for (hcpp::u32 i = 0; i < alt_count; ++i) {
                const AsAlt &a = alts[i];
                if (!a.has_general || !a.has_format || a.ep_num == 0 ||
                    a.ep_mps == 0) {
                    continue;
                }
                // Type-I PCM payloads only, and 16-bit subframes are
                // all the streamer renders.
                if (a.format_tag != format_tag_pcm ||
                    a.fmt_type != fmt_type1_pcm || a.bits != 16 ||
                    a.subframe != 2 || a.channels == 0 ||
                    a.channels > 8) {
                    continue;
                }
                const hcpp::u32 rate = pick_rate(a);
                if (rate == 0) continue;
                int score = 0;
                if (a.channels == 2) score += 4;
                if (rate == 48000) score += 2;
                if (rate == 44100) score += 1;
                if (score > best_score) {
                    best = &a;
                    best_rate = rate;
                    best_score = score;
                }
            }
            if (best != nullptr) rate_out = best_rate;
            return best;
        }

        // Samples one 1 ms frame carries at `rate` (the 44/45-sample
        // alternation for 44.1 kHz falls out of the cumulative math).
        hcpp::u32 frame_samples(hcpp::u32 rate, hcpp::u32 index) noexcept {
            return (rate * (index + 1)) / 1000 - (rate * index) / 1000;
        }

        // Feature-unit volume: read MIN/MAX when available, then
        // SET_CUR a mid-range value on the master channel. All control
        // transfers are individually bounded by the EP0 wait.
        bool fu_volume(HostDevice *dev) noexcept {
            if (!ac.fu_seen || ac.fu_volume_ch == 0xFF) return false;
            volatile hcpp::u8 *s = scratch();
            const hcpp::u16 windex = static_cast<hcpp::u16>(
                (hcpp::u16(ac.fu_id) << 8) | ac.iface);
            // wValue: control selector in the high byte, the channel
            // that actually owns the volume control in the low byte.
            const hcpp::u16 wvalue = static_cast<hcpp::u16>(
                (hcpp::u16(cs_volume) << 8) | ac.fu_volume_ch);
            s[0] = 0;
            s[1] = 0;
            hcpp::i16 vmin = 0;
            hcpp::i16 vmax = 0;
            const bool got_min =
                host_control(dev, 0xA1, req_get_min, wvalue, windex, 2,
                             host_scratch(), true);
            if (got_min) {
                vmin = static_cast<hcpp::i16>(
                    s[0] | (hcpp::u16(s[1]) << 8));
            }
            s[0] = 0;
            s[1] = 0;
            const bool got_max =
                host_control(dev, 0xA1, req_get_max, wvalue, windex, 2,
                             host_scratch(), true);
            if (got_max) {
                vmax = static_cast<hcpp::i16>(
                    s[0] | (hcpp::u16(s[1]) << 8));
            }
            // UAC volume is 1/256 dB signed; take the mid of the
            // reported range (or 0 dB when the range is unknown).
            hcpp::i16 target = 0;
            if (got_min && got_max) {
                target = static_cast<hcpp::i16>(
                    vmin + (vmax - vmin) / 2);
            }
            s[0] = static_cast<hcpp::u8>(target);
            s[1] = static_cast<hcpp::u8>(hcpp::u16(target) >> 8);
            return host_control(dev, 0x21, req_set_cur, wvalue, windex, 2,
                                host_scratch(), false);
        }

    }  // namespace

    void uac_bind(HostDevice *dev, const volatile hcpp::u8 *cfg,
                  hcpp::u32 len) noexcept {
        seen = true;
        if (bound) {
            serial::write_line("uac dev=skip");
            return;
        }
        if (desc_copy_phys == 0) {
            desc_copy_phys = memory::pmm_allocate_page();
        }
        if (desc_copy_phys == 0 || cfg == nullptr) {
            uac_fail(1);
            return;
        }
        volatile hcpp::u8 *copy =
            reinterpret_cast<volatile hcpp::u8 *>(desc_copy_phys);
        const hcpp::u32 n =
            len < memory::page_size ? len : memory::page_size;
        for (hcpp::u32 i = 0; i < n; ++i) copy[i] = cfg[i];
        parse_config(copy, n);

        hcpp::u32 rate = 0;
        const AsAlt *alt = pick_alt(rate);
        if (alt == nullptr) {
            serial::write_line("uac dev=absent");
            return;
        }

        // Select the streaming alt setting device-side, then arm the
        // isoch-OUT endpoint in the slot (transfer ring + endpoint ctx).
        if (!host_control(dev, 0x01, 0x0B, alt->alt, alt->iface, 0, 0,
                          false)) {
            uac_fail(2);
            return;
        }
        if (!host_iso_arm(dev, alt->ep_num, alt->ep_mps,
                          alt->ep_interval)) {
            uac_fail(3);
            return;
        }
        bound = true;
        serial::write_line("uac dev=ok");
        serial::write("uac fmt=");
        serial::write_dec32(alt->channels);
        serial::write("ch/");
        serial::write_dec32(rate);
        serial::write("Hz/");
        serial::write_dec32(alt->bits);
        serial::write("b\r\n");

        // One DMA page of PCM: N consecutive 1 ms frames of a
        // continuous 440 Hz sine at the negotiated rate.
        const hcpp::u64 pcm_phys = memory::pmm_allocate_page();
        if (pcm_phys == 0) {
            uac_fail(4);
            return;
        }
        const hcpp::u32 frame_cap =
            static_cast<hcpp::u32>(memory::page_size) /
            frame_bytes_max(*alt, rate);
        const hcpp::u32 frames =
            frame_cap < iso_frames_max ? frame_cap : iso_frames_max;
        if (frames == 0) {
            uac_fail(5);
            return;
        }
        const hcpp::u32 total_samples = (rate * frames) / 1000;
        audio::waveform_render_sine(
            reinterpret_cast<hcpp::i16 *>(pcm_phys), total_samples, 440,
            rate, alt->channels);

        active_dev = dev;
        stream_ok = false;
        iso_done = 0;
        iso_errors = 0;
        iso_last_code = 0;
        hcpp::u32 off = 0;
        for (hcpp::u32 f = 0; f < frames; ++f) {
            const hcpp::u32 bytes =
                frame_samples(rate, f) * alt->channels * alt->subframe;
            host_iso_queue(dev, pcm_phys + off, bytes);
            off += bytes;
        }
        host_iso_kick(dev);

        const hcpp::u64 start = interrupts::pit_ticks();
        while (iso_done < frames &&
               interrupts::pit_ticks() - start < wait_iso_ticks) {
            usb_poll();
            hype_cpu_halt();
        }
        if (iso_done == frames && iso_errors == 0) {
            stream_ok = true;
            serial::write("uac iso=on frames=");
            serial::write_dec32(frames);
            serial::write("\r\n");
        } else {
            serial::write("uac iso=fail done=");
            serial::write_dec32(iso_done);
            serial::write("/");
            serial::write_dec32(frames);
            serial::write(" code=");
            serial::write_dec32(iso_last_code);
            serial::write("\r\n");
        }
        active_dev = nullptr;

        // Feature-unit volume poke is best-effort and bounded.
        serial::write("uac vol=");
        serial::write_line(fu_volume(dev) ? "ok" : "na");
    }

    void uac_iso_done(HostDevice *dev, hcpp::u8 code) noexcept {
        if (dev != active_dev || code == code_ring_underrun) return;
        ++iso_done;
        if (code != code_success) {
            ++iso_errors;
            iso_last_code = code;
        }
    }

    bool uac_device_seen() noexcept { return seen; }

    bool uac_present() noexcept { return bound && stream_ok; }

}  // namespace hype::usb::usbuac
