# HypeAI OS — Audio/Video Module Research (kernel/audio, kernel/video)

Scope: freestanding x86-64 kernel, C++20, no STL, no third-party code — every target below is a
hand-written `HypeAI.*` implementation. QEMU is the only test harness: it emulates
`intel-hda`+`hda-duplex`, `AC97`, `ES1370`, `sb16`, `adlib`, `cs4231a`, `gus`
(<https://github.com/qemu/qemu/blob/master/docs/qdev-device-use.txt>) and **no** GPU media
engines or capture hardware. Spec hub: <https://wiki.osdev.org/Technical_Specifications>.

Current state: PCI probe + real Intel-HDA bring-up path exist and run at boot
(`audio-serivce.cc` → `hda_*` → 440 Hz tone over BDL, verified via LPIB). Everything else is
API skeletons. `kernel/video` is a V4L2-shaped skeleton: registry + buffer queueing model, no
hardware behind it.

---

## 1. `kernel/audio` — per-file briefs

### `audiopci.cc/h` — PCI audio enumeration
Role/State: scans `pci::devices()` for class 0x04, subclasses HDA/AC'97/all + VirtIO sweep;
implemented (`audio_probe()` at `audiopci.cc:102`).
Doc: PCI class codes <https://wiki.osdev.org/PCI#Class_Codes>; IDs <https://pci-ids.ucw.cz/read/PD/04>.
Add: prog-if field, MSI cap check, cached IRQ line, sibling `is_video_device()` for subclass 0x0400.
QEMU: full — all emulated cards enumerate.

### `audiopcicard.cc/h` — `AudioCard` model + registry
Role/State: chip enum (HDA/AC97/ES137x/VirtioSound/SB16/AdLib) + card array (max 8) shared by
PCI/ISA buses; register/iterate implemented.
Doc: OSDev Sound overview <https://wiki.osdev.org/Sound>.
Add: `audio_card_set_driver_state()`, DMA mask field, unplug stub.
QEMU: n/a (data model).

### `audiomaps.h` — PCI ID → chip table
Role/State: `constexpr AudioIdMap[]` (8086:2668/293E HDA, 8086:2415 AC97, 1274:5000/1371 ES137x)
+ class/vendor constants; small but correct.
Doc: QEMU device sources <https://github.com/qemu/qemu/tree/master/hw/audio>.
Add: only QEMU-observed IDs; later Realtek/ICH9 IDs when porting to HW.
QEMU: full.

### `audio-serivce.cc/h` — service init (rename typo `serivce`→`service`)
Role/State: boot orchestrator: probe → register → per-chip bring-up → serial log; HDA-only today,
LPIB-poll verify (`audio-serivce.cc:28-42`).
Doc: internal; pattern mirrors `core_services.cc`.
Add: `chip → bringup_fn` dispatch table, AC97/ES1370/virtio-snd branches, `audio_format_selftest()` body.
QEMU: full (serial-logged, already exercised).

### `hda.cc/h` — Intel HDA controller driver (23.7 KB, the real driver)
Role/State: HDA host driver: GCAP/GCTL, CORB/RIRB rings,
codec verbs, stream descriptors, BDL DMA, AFG→DAC→pin + ADC→pin capture walks,
48 kHz s16 stereo playback + capture, IOC interrupts, amp get/set
(`hda.h:7-190`); produces verified LPIB motion both directions.
Doc: Intel HDA Spec 1.0a <https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf>; OSDev <https://wiki.osdev.org/Intel_High_Definition_Audio>.
Done: renamed `hda.cc/hda.h`, capture stream, IOC interrupts (INTSTS-polled count),
`hda_volume_get/set()` amp verbs, `hda_topology_walk()`. Add: LPIB-wrap handling,
multi-stream.
QEMU: full — `intel-hda`+`hda-duplex` faithful; MSI works.

### `audiofudev.h` — HDA widget/function topology walker
Role/State: "function device" enumeration into `AudioFunctionNode[]` (card/codec/node/group/
vendor/caps); stores caps only today.
Doc: HDA Spec §7.3 (widget params, connection lists), same PDF as above.
Add: full walk `param_node_count`→children→`connlist` graph; programmatic DAC→pin path picker.
QEMU: full — hda-duplex exposes a small fixed widget tree (2 AFGs, DAC/ADC, 2 pins).

### `isau.cc/h` — ISA-era audio table (rename → `audioisa.cc/h`)
Role/State: static card table: SB16 (0x220/IRQ5/DMA1+5), AdLib OPL2 (0x388), WSS (0x530); no
port probing or DSP code yet.
Doc: SB16 DSP <https://wiki.osdev.org/Sound_Blaster_16>; YM3812 datasheet
<https://map.grauw.nl/resources/sound/yamaha_ym3812_datasheet.pdf>; CS4231A datasheet (Cirrus).
Add: `isa_probe()` (DSP reset 0x226, expect 0xAA on 0x22A, version read), DMA1 PCM program,
OPL2 register-write + timer.
QEMU: yes for sb16/adlib; WSS not emulated (data-only). Effort: SB16 moderate, AdLib trivial.

### `audioapi.h` — public stream API
Role/State: `AudioFormat`, `audio_stream_open/write/set_volume/close`; write() lands in ring,
no per-stream HW drain yet.
Doc: WASAPI shared/exclusive-mode mental model
<https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi>.
Add: `pause/resume`, `position()`, `set_format()` renegotiation, poll/event handle. — QEMU: n/a.

### `audiocore.cc` — mixer + stream/client/decoder/funnode tables (34 KB)
Role/State: central state: 4 streams, 8 clients, decoder table, stage buffer, WAV probe→tone,
volume; mix primitives call `audio.asm`.
Doc: internal.
Add: periodic drain loop (timer/IRQ pull: stream→mix stage→HDA BDL pages), resampler
(any-rate→48 k linear), per-stream gain pre-mix, underrun counter.
QEMU: full via hda-duplex; `-audiodev none` still advances LPIB.

### `audiobuf.h` — SPSC byte ring (8 KiB)
Role/State: `AudioRing` push/pop used as per-stream staging; ~42 ms headroom at 48 k stereo.
Doc: classic ring; Linux `kfifo` semantics as reference.
Add: frame-granularity `push_frames/pop_frames`, high-water mark, overrun counter. — QEMU: n/a.

### `audio.asm` — PCM mix primitives (NASM64)
Role/State: `hype_audio_buffer_clear`, `hype_audio_mix_s16` (saturating add),
`hype_audio_apply_gain_s16`; scalar loops implemented.
Doc: Intel SDM Vol 1 (SSE2) <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>;
OSDev <https://wiki.osdev.org/SSE>.
Add: SSE2 `paddsw` (free clip) and `pmulhw` (gain) paths, mono-mix + u8/f32 variants.
QEMU: n/a (CPU-only); verify on HW later.

### `audio-pcm.asm` — PCM format shims (NASM64)
Role/State: `hype_audio_pcm_copy`, `mono_to_stereo_s16`, `swap_channels_s16`; scalar.
Doc: same SSE refs.
Add: SSE2 `punpcklwd` mono→stereo, `pshuflw/hw` channel swap, s16→f32/u8 converters. — QEMU: n/a.

### `lazefg.cc/h` — gain ramps/fades (rename → `audiofx.cc/h`)
Role/State: cryptic ≈ fade/gain: Q8.8 linear ramp on s16 buffer, `fade_in/out`, `clip`;
implemented (`lazefg.cc:5-35`).
Doc: pan-law note <https://www.cs.cmu.edu/~music/icm-online/readings/panlaws/>.
Add: equal-power ramp option, dB-domain ramp, `audiofx_pan()` stereo weight; rename prefix. — QEMU: n/a.

### `waveform.h` — tone synthesis
Role/State: phase-accumulator parabolic sine (q15), `waveform_tone/next`; feeds the HDA test
tone today.
Doc: DDS/NCO — Analog MT-085 <https://www.analog.com/media/en/training-seminars/tutorials/MT-085.pdf>.
Add: `square/saw/triangle`, block API `waveform_render(buf,tone,n)`, 32-bit phase. — QEMU: n/a.

### `audiohigh.h` — high-level playback facade
Role/State: `audio_play_tone/play_buffer`, master volume; thin layer over audiocore.
Doc: WASAPI shared-mode semantics (link above).
Add: `audio_play_file(path)` via codec registry, completion flag, stop/abort.
QEMU: full via HDA tone path.

### `audioclient.h` — client registry
Role/State: named clients → stream binding (future multi-app mixing); table ops implemented.
Doc: internal.
Add: per-client volume, name dedup, ring ownership. — QEMU: n/a.

### `alsaau.h` — ALSA-shaped PCM shim
Role/State: `alsa_pcm_open/writei/prepare/close` — familiarity layer over `snd_pcm_*` shape;
`alsa_error=-1`.
Doc: ALSA lib PCM API <https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html>.
Add: `drop/drain/avail` (ring space), `hw_params` struct, errno-style code table. — QEMU: n/a.

### `audioparse.h` — WAV/RIFF parser
Role/State: `wav_parse`→`WavInfo` + `audio_tag("RIFF")` fourcc helper; parse implemented.
Doc: RIFF/WAV <https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-formats>;
fmt chunk <http://soundfile.sapp.org/doc/WaveFormat/>.
Add: `riff_next_chunk()` walker, extensible tag 0xFFFE, `wav_write()` encoder (header+copy),
IMA-ADPCM tag → `codecapps/audio/adpcm.cc`. — QEMU: n/a.

### `dvdeca.h` — decoder-entry registry (rename → `audiocodec.cc/h` or delete)
Role/State: `AudioDecoderEntry{codec,name,probe}`; ids Pcm/WavPcm/Alac/Mp2/Ac3 — "DVD-eca" =
DVD audio pair MP2+AC3. Overlaps `kernel/codecapps/registry.cc`.
Doc: MP2 = ISO/IEC 11172-3 / header ref <https://www.mpgedit.org/mpgedit/mpeg_format/mpeghdr.htm>;
AC-3 = ATSC A/52 <https://www.atsc.org/wp-content/uploads/2015/03/A52-201212-17.pdf>.
Add: collapse into `codec_probe()` bridge — map `AudioCodecId`→`CodecId`, single registry. — QEMU: n/a.

### `alac.h` — ALAC config ("magic cookie")
Role/State: `AlacConfig` (depth/channels/rice/frame), `alac_parse_cookie`, 24-byte cookie,
`alac`/`frma` magics; implemented.
Doc: Apple ALAC source <https://github.com/macosforge/alac>; FFmpeg
<https://ffmpeg.org/ffmpeg-codecs.html#alac>.
Add: `alac_write_cookie()` serializer, bitstream-limit validation table. — QEMU: n/a.

### `alacdecoder.h` — ALAC decoder core
Role/State: `AlacBitReader` (MSB-first) + status enum + scaffolding; Rice/LPC stages pending.
Doc: same sources; frame layout only fully defined by `alac.c`/`aglib` source (tag→channels→
wasted→verbatim|interlaced→Rice residuals→predictor).
Add: `alac_decode_frame()` covering setinfo, Rice decode (history/init-hist/k), un-M/S,
16/24-bit out; wire into `codecapps/audio/alac_codec.cc`.
QEMU: n/a; test vectors from Apple's host-side encoder.

### `alacencdoer.h` — ALAC encoder stub (rename typo → `alacencoder.h`)
Role/State: `AlacEncoderState`, `alac_encoder_supported()=false`; declared only.
Doc: Apple `aglib` encoder (same repo); simplest legal output = verbatim or fixed-predictor
Rice frames — validity defined by decoder acceptance.
Add: verbatim-mode encoder first (lossless pass-through), then fixed-LPC; flip `supported()`
after round-trip selftest. — QEMU: n/a.

---

## 2. `kernel/video` — per-file briefs (.cc/.h pairs)

### `videoframe.cc/h` — frame model
Role/State: `PixelFormat{RGB24,RGB32,YUV420,NV12,Compressed}` + `VideoFrame` + size helpers;
implemented.
Doc: fourcc catalogue <https://www.fourcc.org/>; V4L2 pixelformats
<https://docs.kernel.org/userspace-api/media/v4l/pixfmt.html>.
Add: `frame_plane_count()`, per-plane stride/offset (needs `plane[3]` not single ptr), `frame_copy()`. — QEMU: n/a.

### `videoyuv.cc/h` — YUV→RGB
Role/State: `yuv_clamp`, `yuv420_to_rgb24`; implemented — verify BT.601 vs 709 coefficients.
Doc: ITU-R BT.601 coefficients via <https://www.fourcc.org/fccyvrgb.php>; ffmpeg swscale tables.
Add: `nv12_to_rgb24`, `yuy2_to_rgb24` (UVC emits YUY2), BT.709 flag, SSE2 path. — QEMU: n/a.

### `videostream.cc/h` — buffer queueing (QBUF/DQBUF model)
Role/State: 8-buffer queue, `stream_queue/dequeue/start/stop`; bookkeeping done, no producer.
Doc: V4L2 streaming I/O <https://docs.kernel.org/userspace-api/media/v4l/mmap.html>.
Add: `dqbuf` timeout, phys-page buffer import, per-buffer pixfmt. — QEMU: n/a.

### `videov4lv.cc/h` — "V4L-virtual" device layer
Role/State: `VideoDeviceInfo{id,caps,w,h,fmt}` registry, caps Capture/Output/Streaming/
HardwareDecode; implemented (8 slots).
Doc: V4L2 spec <https://docs.kernel.org/userspace-api/media/v4l/v4l2.html> (device caps model).
Add: `v4lv_unregister`, `videoN` devnode naming, cap-query entry. — QEMU: n/a.

### `videov4lvstate.cc/h` — device state machine
Role/State: Stopped→Open→Streaming→Error per device id; implemented.
Doc: V4L2 STREAMON/STREAMOFF semantics (same spec).
Add: transition guard table, `v4lv_state_name()` for serial logs. — QEMU: n/a.

### `videov4lv_key.cc/h` — fourcc keys
Role/State: `v4lv_key_make('Y','U','1','2')`→u32 pixfmt id; implemented.
Doc: <https://www.fourcc.org/yuv.php>.
Add: named constants `yuyv/nv12/mjpg`, reverse `v4lv_key_name()`. — QEMU: n/a.

### `videocapture.cc/h` — capture pipeline
Role/State: `capture_copy_frame/convert_frame`; copy path done, no source device exists.
Doc: UVC 1.5 (future webcams) <https://www.usb.org/document-library/video-class-v15-document-set>.
Add: `capture_read_frame(dev,frame)` via `videostream` dequeue; test-pattern source.
QEMU: **no capture HW exists in QEMU** — virtual-device only (`videovirtual`), or UVC+host
webcam passthrough once xHCI lands.

### `videovirtual.cc/h` — virtual test device
Role/State: registers fake 1024×768 RGB24 Output|Streaming device; implemented.
Doc: model on Linux `vivid` <https://docs.kernel.org/admin-guide/media/vivid.html>.
Add: SMPTE-bars/moving-box pattern generator writing into queued stream buffers on a timer —
this is what makes capture testable without HW.
QEMU: full (pure SW).

### `videoclient.cc/h` — client/session layer
Role/State: open/close handles vs v4lv ids; minimal.
Doc: internal (V4L2 file-handle model).
Add: `video_client_stream(dev)`, per-client queue ownership, exclusive flag. — QEMU: n/a.

### `videocontrol.cc/h` — controls (brightness/contrast/…)
Role/State: `control_set/get(dev,id,val)` — VIDIOC_S_CTRL analogue; stub.
Doc: V4L2 controls <https://docs.kernel.org/userspace-api/media/v4l/control.html>.
Add: range table (min/max/default/step), apply to virtual pattern gen, `control_list()`. — QEMU: n/a.

### `videoformat.cc/h` — format negotiation
Role/State: `format_supported/negotiate/name` — VIDIOC_S_FMT analogue; basics done.
Doc: <https://docs.kernel.org/userspace-api/media/v4l/vidioc-g-fmt.html>.
Add: `format_enum()` list, stepwise frame-size enum, non-destructive `format_try()`. — QEMU: n/a.

### `videodecoder.cc/h` — decode dispatch
Role/State: `CodecType{Unknown,Raw,Lz,Mpeg2,Hardware}` + `DecodeResult` + `video_decode()`;
dispatcher skeleton.
Doc: FFmpeg avcodec send/receive shape <https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html>.
Add: per-codec framer (frame-boundary split for startcode streams), `video_decode_flush()`. — QEMU: n/a.

### `videodecoderlz.cc/h` — LZ frame codec
Role/State: `lz_decode_frame`; stub — presumably hypeai's own LZ/RLE compressed-frame format
for internal capture/remote display.
Doc: own format; LZ77 ref <https://wiki.osdev.org/LZ77>; if LZ4-like,
<https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md>.
Add: define header (magic `LZF1`, w/h/fmt, literal/backref blocks), decode+encode pair,
`mcodec_register(CodecType::Lz,…)`.
QEMU: n/a; round-trip selftest.

### `videodecoderpcie.cc/h` — "PCIe decode" stub → rename `hwdecode.*`
Role/State: `pcie_decoder_available()=false`, decode always fails — seat for GPU offload;
see §3: permanent stub on this platform.
Doc: §3 refs (VAAPI/VCN/NVDEC/virtio-video).
Add: don't chase HW — define `DecodeJob{codec,bitstream_phys,len,ref_frames[4],dst_frame}`
descriptor ABI behind `CodecType::Hardware` for a future virtio-video/real driver.
QEMU: none (§3).

### `videomcodec.cc/h` — codec registry (mini)
Role/State: `mcodec_register/find/count`; parallel to `codecapps/registry.cc`.
Doc: internal.
Add: merge into `kernel/codecapps` registry (new `CodecKind::Video`) or keep as HW-path table;
register Raw/Lz/Mp2 builtins at init. — QEMU: n/a.

### `videomp2.cc/h` — MPEG-2 video probe
Role/State: startcode detect (00 00 01) + probe only; this is MPEG-2 **video**, distinct from
MP2 audio in `dvdeca`.
Doc: ITU-T H.262 | ISO/IEC 13818-2 (free PDF) <https://www.itu.int/rec/T-REC-H.262/en>;
startcodes <https://wiki.multimedia.cx/index.php/MPEG-2>.
Add: sequence header parse (w/h/aspect/fps @0xB3), GOP header; decoder later: I-frames first
(intra VLC + IDCT, no MC) → P-frames. Large but all integer math.
QEMU: n/a (CPU-bound SW decode; fine).

### `videoraw.cc/h` — raw framebuffer codec
Role/State: `raw_frame_accept/clear` — pass-through for `CodecType::Raw`; implemented.
Doc: n/a.
Add: `raw_encode()` = header+memcpy; register as default mcodec fallback. — QEMU: n/a.

### `videoemuxer.cc/h` — demuxer (typo for demuxer)
Role/State: `StreamPacket{data,size,stream,ts}` + `probe/codec`; stub, no container chosen.
Doc: MPEG-TS (ISO/IEC 13818-1, 188 B packets — easiest real-world); Matroska/WebM RFC 9559
<https://www.rfc-editor.org/rfc/rfc9559> + EBML RFC 8794; AVI/RIFF cheapest
<https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference>.
Add: `emuxer_open(hint)`, PAT/PMT walk (TS), EBML element reader (MKV), `emuxer_next_packet()`.
QEMU: n/a (parse files via fs).

### `videocoretest.cc/h` — selftest
Role/State: `video_core_selftest()` stub.
Doc: n/a.
Add: v4lv register → virtual pattern → stream queue → yuv convert → LZ round-trip → serial PASS;
wire into boot selftest like `codec_probe_selftest()`.
QEMU: full.

---

## 3. PCI media-decode hardware — reality check

**Real hardware:** Intel Quick Sync / Media Engine (fixed function inside the GT block; driven
via i915 + GuC firmware; userspace ABI = VA-API/libva <https://intel.github.io/libva/>); AMD
VCN (amdgpu + AMF <https://gpuopen.com/advanced-media-framework/>); NVIDIA NVDEC (proprietary,
Video Codec SDK only <https://docs.nvidia.com/video-technologies/video-codec-sdk/>). None are
documented at register level for OS authors; all need vendor firmware blobs.

**QEMU: none of it exists.** QEMU emulates `VGA/bochs-display/virtio-gpu` display devices —
no MFX/VCN/NVDEC block, and Intel publishes the display controller docs, not the media-engine
regs. There is no way to test a PCIe decode driver in QEMU and no spec to write one against.
→ `videodecoderpcie.*` stays a stub; rename to `hwdecode.*` and let it hold the
**descriptor-ready ABI** (`DecodeJob` above) that SW decode and any future offload share.

**One exception — virtio-video:** OASIS virtio drafts define a video device (class 0x04,
vendor 0x1AF4); QEMU ≥8.2 can expose it via **vhost-user-video** backend
<https://www.qemu.org/docs/master/interop/vhost-user.html> — but the host daemon does the
decoding, so it's an RPC harness, not hardware docs. A thin descriptor-queue driver is a valid
later ticket.

**PCI class codes for this domain** (class 0x04 multimedia, <https://pci-ids.ucw.cz/read/PD/04>):
`0x0400` multimedia video (old BT8x8 capture — not emulated) · `0x0401` multimedia audio
(AC'97 82801AA, ES1370) · `0x0402` telephony · `0x0403` HD Audio (intel-hda) · `0x0480` other.
VirtIO-snd matches by vendor 0x1AF4/device 0x1059 rather than class.

---

## 4. Codec gaps vs `kernel/codecapps` registry

Registry: 25 real ids (`codec.h:11-39`) — images bmp…exif (png/jpeg/qoi decode-complete per
`codecapps/image/`), audio wav/adpcm/flac/mp3/vorbis/opus/alac/aac, containers ogg/mp4.
`CodecKind` has **no Video kind** — first gap to fix (`u8` enum, `CodecId` headroom to 255).

Next additions, ordered by effort/value:

- **FLV container** — trivial tag parser; pairs with `videoemuxer`. Spec:
  <https://veovera.org/docs/legacy/video-file-format-v10.pdf>.
- **AVI/RIFF container** — chunk walker mostly exists from WAV; feeds MJPEG.
- **MJPEG video** — reuse existing JPEG decoder per frame; simplest real video path.
- **FFV1 video** — RFC 9043 <https://www.rfc-editor.org/rfc/rfc9043>; royalty-free lossless,
  range coder + median prediction; ideal own-implementation target.
- **MPEG-TS demux** (13818-1) — PAT/PMT; pairs with `videomp2`.
- **IMA-ADPCM-in-WAV** — extend `adpcm.cc` to WAV tag 0x11; trivial.
- **MIDI/SMF parse + OPL2 render** — <https://midi.org/specs>; feeds the AdLib driver — very on-brand.
- **Matroska/WebM** (RFC 9559) — after EBML reader lands.
- **PCX image** (trivial RLE, <http://www.qzx.com/pc-gpe/pcx.txt>) and **XBM** for icon tests.
- Defer w/ links noted: H.264/HEVC, AV1, JPEG XL, WebP-lossy (VP8), Theora — months-scale entropy
  coding; revisit after MPEG-2.

---

## 5. P11 ticket breakdown (10 tickets)

| # | Ticket | Files grouped | Deliverable |
|---|---|---|---|
| P11-1 | HDA completion + rename | `izau.*`→`hda.*`, `audiofudev`, `audio-serivce.*` | capture stream, IOC IRQ, amp verbs, topology walk, rename |
| P11-2 | AC'97 driver | `audiopci.*`, `audiomaps.h`, new `audioac97.*` | NABM regs + BD ring, 48 k PCM out; OSDev <https://wiki.osdev.org/AC97> + AC'97 r2.3 spec; QEMU `-device AC97` |
| P11-3 | ES1370/71 driver | `audiomaps.h`, new `audioes1370.*` | 5880 regs, DAC ring; QEMU `-device ES1370` |
| P11-4 | ISA audio: SB16 + AdLib | `isau.*`→`audioisa.*`, `waveform.h`, new `sb16.*`, `opl2.*` | DSP reset/version, 8237 DMA PCM; OPL2 reg API + FM patches for SMF |
| P11-5 | PCM mixer core | `audiocore.cc`, `audiobuf.h`, `audio.asm`, `audio-pcm.asm`, `lazefg.*`→`audiofx.*`, `audioapi.h`, `audioclient.h` | IRQ drain, SSE2 mix, resampler, per-stream gain, underrun stats |
| P11-6 | Audio API surface + WAV | `audiohigh.h`, `alsaau.h`, `audioparse.h`, `dvdeca.h` | `audio_play_file`, ALSA compat calls, RIFF walker, dvdeca→registry bridge |
| P11-7 | Audio codec bodies | `alac*.*` (rename `encdoer`→`encoder`), `codecapps/audio/*` | ALAC decode (Rice+LPC+M/S), verbatim encoder, MP2/AC3 probes→registry |
| P11-8 | Video core + virtual capture | `videoframe`, `videoyuv`, `videostream`, `videov4lv*`, `videoclient`, `videocontrol`, `videoformat`, `videovirtual`, `videocoretest` | bars pattern gen, queue→convert→selftest pipeline, control ranges, state guards |
| P11-9 | Demux + containers | `videoemuxer.*`, `videomp2.*`, `videodecoder.*`, `videomcodec.*` | TS/AVI/EBML splitter, MPEG-2 seq-header parse, `CodecKind::Video` + registry merge |
| P11-10 | SW decode ladder + HW ABI | `videodecoderlz.*`, `videoraw.*`, `videodecoderpcie.*`→`hwdecode.*` | LZF1 round-trip, MJPEG via JPEG, FFV1 decode, `DecodeJob` ABI doc |

Ordering preserves the roadmap note (`docs/roadmap.md` P11: MJPEG → MPEG-1/2 → Theora →
H.264-baseline): tickets 9–10 carry it through MPEG-2 I-frames + FFV1; Theora/H.264 explicitly
out of scope for P11.
