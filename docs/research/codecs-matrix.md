# Codec / File-Format Research Matrix

**Date:** 2026-02-10
**Scope:** Format detection, container structure, decode-algorithm sketch, minimal reference
implementations (size + license), freestanding-kernel feasibility, and implementation
ordering for `kernel/codecapps` (CodecsApps).
**Companion doc:** `docs/research/codecs-formats.md` holds the deep decoder skeleton specs
(memory budgets, ABI, selftests). This doc is the wide survey/matrix view — do not
duplicate the skeletons here.

Environment assumptions (bare-metal x86_64, freestanding C++, no libc, kernel context):

- VFS exposes whole-file reads only (`fs_read`, `kernel/vfs/hypefs.h`). Everything below
  assumes the full file is resident in a buffer; streaming/pull decode is future work.
- All decoders receive scratch memory through `Arena &` (`kernel/codecapps/arena.cc`),
  never `malloc`.
- Decode ABI (`kernel/codecapps/codec.h`):
  `CodecStatus fmt_decode(const u8* in, size_t n, void* out /*FrameBuf|PcmBuf*/, Arena&)`.
- Output surfaces: `FrameBuf` → `xdp_surface` BGRA blit; `PcmBuf` → `src_convert` →
  48 kHz pullmix (`kernel/audio/pullmix.h`).
- DEFLATE: own inflate exists at `kernel/codecapps/image/inflate.cc`; a full
  inflate+deflate lib (`kernel/ztext`, "ZText") is landing in parallel — assume it
  available for everything marked "needs inflate".
- A `CodecId` + probe in `kernel/codecapps/magic.cc` + a row in `builtin_descriptors`
  is the registration ceremony for every new format.

Effort scale: **S** <400 LOC · **M** 400–1500 · **L** 1.5k–5k · **XL** >5k (kernel-style
decoder only, excluding container plumbing already shared).

---

## 1. Detection matrix — magic bytes

`@N` = byte offset from file start. All multi-byte constants big-endian as stored.
Probe-order rule (already encoded in `magic.cc`): specific signatures before generic —
`adpcm` before `wav`, `flac`/`vorbis`/`opus` before `ogg`, `alac`/`avif`/`heic` before
`mp4`. New probes must extend `probe_cases` selftest (`magic.cc:313`).

| Format | Offset | Signature | Probe note / existing status |
|---|---|---|---|
| PNG / APNG | 0 | `89 50 4E 47 0D 0A 1A 0A` | done; APNG adds `acTL` chunk — same probe |
| JPEG / MJPEG frame | 0 | `FF D8 FF` | done (baseline); progressive SOF2 → `Unsupported` |
| GIF 87a/89a | 0 | `47 49 46 38 37/39 61` (`GIF8?a`) | done |
| BMP | 0 | `42 4D` (`BM`) | done |
| ICO | 0 | `00 00 01 00` | done; CUR = `00 00 02 00` (same probe) |
| WebP | 0+8 | `52 49 46 46` … `57 45 42 50` (`RIFF????WEBP`) | probe only |
| TIFF | 0 | `49 49 2A 00` or `4D 4D 00 2A` (`II*.`/`MM.*`) | done (baseline+PackBits) |
| TGA | EOF−26 | `54 52 55 45 56 49 53 49 4F 4E 2D 58 46 49 4C 45 2E 00` | footer, not header — `codec_probe_file` must read tail; header is weak (only `imgtype` ∈ {1,2,3,9,10,11} is distinctive) |
| QOI | 0 | `71 6F 69 66` (`qoif`) | done |
| PNM/PPM | 0 | `50` + `31..36` (`P1`–`P6`) | done |
| PSD | 0 | `38 42 50 53` (`8BPS`) | probe only |
| JPEG 2000 | 0 | `FF 4F FF 51` or `00 00 00 0C 6A 50 20 20` | probe only |
| AVIF | 4/8 | `66 74 79 70` + brand `avif`/`avis` | probe only |
| HEIC | 4/8 | `ftyp` + `heic`/`heix`/`hevc`/`mif1` | probe only |
| WAV | 0+8 | `RIFF????WAVE`; fmt tag @20 | done (PCM); ADPCM pre-probe checks tag `0x11/0x06/0x07` |
| AIFF | 0+8 | `46 4F 52 4D ???? 41 49 46 46` (`FORM????AIFF`) | not registered — add `CodecId::Aiff` |
| FLAC | 0 | `66 4C 61 43` (`fLaC`); Ogg-FLAC: `OggS` + `7F 'FLAC'` @28 | done via `dr_flac` |
| MP3 | 0 | `FF FB/FA` (`b[1]&0xE0==0xE0`, layer `01`) or `49 44 33` (`ID3`) | done via `minimp3` |
| MP2 | 0 | `FF F?` layer bits `10`, version ≠ `01` | probe only |
| AAC (ADTS) | 0 | `FF F1`/`FF F9` (`b[1]&0xF6==0xF0`) | probe only |
| AC-3 | 0 | `0B 77` | probe only |
| Ogg Vorbis | 0+28 | `4F 67 67 53` + `01 'vorbis'` @28 | done via `stb_vorbis` |
| Ogg Opus | 0+28 | `OggS` + `OpusHead` @28 | probe only |
| Ogg (generic) | 0 | `4F 67 67 53` | demux done `audio/ogg.cc` |
| MIDI SMF | 0 | `4D 54 68 64` (`MThd`) | no `CodecId` yet |
| MOD (ProTracker) | 1080 | `4D 2E 4B 2E` (`M.K.`), `M!K!`, `FLT4`, `xCHN`, `xxCH` | tail-offset probe like TGA |
| XM | 0 | `45 78 74 65 6E 64 65 64 20 4D 6F 64 75 6C 65 3A 20` (`Extended Module: `) + `1A` @37 | no `CodecId` yet |
| S3M | 44 | `53 43 52 4D` (`SCRM`) | no `CodecId` yet |
| IT | 0 | `49 4D 50 4D` (`IMPM`) | no `CodecId` yet |
| AVI | 0+8 | `RIFF????AVI ` | probe only |
| MKV / WebM | 0 | `1A 45 DF A3`; DocType `matroska`/`webm` in EBML header | probe only |
| MP4 / MOV | 4 | `66 74 79 70` + brand `isom/iso2/mp4?/qt  /M4A /avc1` | probe only |
| MPEG-TS | 0,188,376 | `47` sync at packet stride 188 | probe only |
| MPEG-PS / MPEG video | scan ≤256 | `00 00 01 BA` (pack) / `00 00 01 B3` (seq hdr) | MPEG-2 seq probe exists |
| H.264 Annex-B | 0 | `00 00 00 01` + NAL type 7 (SPS `6x`) | probe only |
| YUV4MPEG (y4m) | 0 | `59 55 56 34 4D 50 45 47 32` (`YUV4MPEG2`) | no `CodecId` yet |
| FLV | 0 | `46 4C 56 01` (`FLV\x01`) | optional, low value |
| ZIP | 0 | `50 4B 03 04` (`PK\x03\x04`; also `PK\x05\x06` empty) | no `CodecId` yet — add `Archive` kind |
| GZIP | 0 | `1F 8B` | no `CodecId` yet |
| TAR | 257 | `75 73 74 61 72` (`ustar`/`ustar\0`/`ustar  `) | mid-file probe like MOD/TGA |
| 7z | 0 | `37 7A BC AF 27 1C` | no `CodecId` yet |
| XZ | 0 | `FD 37 7A 58 5A 00` | no `CodecId` yet |
| bzip2 | 0 | `42 5A 68` (`BZh`) | skip — niche |
| RAR | 0 | `52 61 72 21 1A 07` | skip — proprietary codec chain |
| TTF | 0 | `00 01 00 00` or `74 72 75 65` (`true`) | `kernel/font/ttf.cc` parses already |
| OTF (CFF) | 0 | `4F 54 54 4F` (`OTTO`) | glyf rasterizer done; CFF not |
| TTC | 0 | `74 74 63 66` (`ttcf`) | not probed |
| WOFF / WOFF2 | 0 | `77 4F 46 46` / `77 4F 46 32` | no `CodecId` yet |
| PDF | 0 | `25 50 44 46 2D` (`%PDF-`) | out of scope |

---

## 2. Image formats

### PNG — done, `kernel/codecapps/image/png.cc`
8-byte signature → chunk stream `(u32 len, u32 type, data, u32 crc32)`; `IHDR` carries
w/h/bitdepth/colortype/interlace; `PLTE`/`tRNS` optional; concat all `IDAT` → zlib
stream → inflate → per-scanline filter byte → unfilter (None/Sub/Up/Average/**Paeth**).
Color types 0/2/3/4/6, depths 1–16 → expand to BGRA32.
Adam7 interlace = 7 fixed passes over the same unfilter core; APNG adds
`acTL`/`fcTL`/`fdAT` and reuses the whole pipeline per frame.
*Effort:* M (done); Adam7+APNG extension S. *Feasible:* yes — needs only inflate.
*Refs:* own code (W3C PNG-3 REC now folds APNG into the spec); alternative single-file
reference `lodepng` (~6.5k LOC, zlib license) if a second implementation is wanted for
selftest cross-checks.

### JPEG baseline — done, `kernel/codecapps/image/jpeg.cc` (NanoJPEG-derived)
Marker walk `SOI→APPn/DQT/SOF0/DHT/DRI/SOS→EOI`; per 8×8 block: Huffman DC-diff + AC
RLE pairs, zigzag, dequantize, fixed-point IDCT; 4:4:4/4:2:2/4:2:0 chroma upsample,
YCbCr→RGB. Progressive (`SOF2`) and lossless (`SOF3`) intentionally rejected.
*Effort:* L (done ~660 LOC baseline). *Refs:* **NanoJPEG** v1.3.5+ — ~900 LOC, **MIT**
(older ≤1.3 used the restrictive "KeyJ Research License" — do not import those);
**jpgd** (richgel999 jpeg-compressor) ~3.3k LOC, public domain; **stb_image.h**
7,798 LOC, PD/MIT covers JPEG+PNG+GIF+TGA+BMP+PSD+HDR in one header.
*Hardening list:* restart-marker resync, EXIF orientation via `image/exif.cc`.

### GIF 87a/89a — done (still frame), `kernel/codecapps/image/gif.cc`
Header → Logical Screen Descriptor (packed fields: GCT flag/size) → global color table
→ block loop: `0x2C` image descriptor (local palette, interlace flag) → LZW sub-blocks;
`0x21 F9` Graphic Control Ext (delay cs, transparency idx, disposal); `0x21 FF` app ext
(`NETSCAPE2.0` loop count); `0x3B` trailer.
LZW: min-code-size 2–8, clear=`1<<mcs`, codes grow to 12 bits, prefix/suffix stacks.
Interlace pass row-order (0,8)(4,8)(2,4)(1,2).
*Effort:* M done; animation iterator + disposal compositing S. *Refs:* spec-gif89a.txt;
from-scratch already chosen — GIF LZW differs from TIFF/ZIP LZW variants only in
parameter packing.

### BMP — done, `kernel/codecapps/image/bmp.cc`
`BM` + 14-B file header + DIB (`BITMAPINFOHEADER` 40 B or v4/v5): bpp 1/4/8 paletted,
16/24/32 direct, bottom-up rows, RLE8/RLE4 and bitfields variants.
*Effort:* XS — trivia-grade, already encode+decode (screenshot writer reuses it).

### ICO / CUR — done (ICO), `kernel/codecapps/image/ico.cc`
Dir header `reserved,type,count` + N×16 B `ICONDIRENTRY` → payload is either PNG
(sig-dispatch → `png_decode`) or BMP-DIB (height field = 2× icon h: XOR bitmap + 1-bit
AND mask → alpha). CUR reuses the same container; `planes/bpp` fields become hot-x/y.
*Effort:* XS; remaining work = return hotspot pair for `kernel/input/cursprite.cc`.

### WebP — probe only, propose `kernel/codecapps/image/webp.cc`
RIFF container: `VP8 ` lossy / `VP8L` lossless / `VP8X` extended (+`ALPH`,`ANIM`,`ICCP`).
**VP8L** (lossless): LZ77 variant + two-level prefix codes + color cache + 4 invertible
transforms (predictor, color, subtract-green, color-indexing). Spec now public in
**RFC 9649** — from-scratch feasible and shares machinery with inflate.
**VP8** (lossy): boolean arithmetic decoder, intra prediction, 4×4 DCT/WHT, loop
filter — a video codec core, honest XL in disguise.
*Effort:* M–L for VP8L; XL for `VP8 `+`VP8X`/animation. *Feasible:* VP8L yes.
*Refs:* **Wuffs** std/webp has a VP8L decoder (transpiled single-file C,
Apache-2.0/MIT); `libwebp` BSD-3 but far too heavy.

### TIFF baseline — done, `kernel/codecapps/image/tiff.cc`
Byte-order marker (`II`/`MM`) + magic 42 → IFD chain of 12-B tag entries → strips/tiles.
Baseline = uncompressed + PackBits; common extensions LZW/Deflate/JPEG-in-TIFF.
*Effort:* M (done); LZW strips are a small add-on. *Refs:* TIFF 6.0 spec (loc.gov
mirror). Multi-page → expose page count for fax/scanned docs later.

### TGA — done, `kernel/codecapps/image/tga.cc`
18-B header (idlen, cmaptype, imgtype, cmap, x/y/w/h/bpp/desc) + raw or RLE-packet
raster, 8/16/24/32-bpp, palette+gray+truecolor. Probe via `TRUEVISION-XFILE.` footer.
*Effort:* XS — done.

### QOI — done, `kernel/codecapps/image/qoi.cc`
14-B header + opcode stream (`INDEX`/`DIFF`/`LUMA`/`RUN`/`RGB`/`RGBA`) over a 64-entry
running pixel-index hash. Decoder ≈100 LOC. *The* reference first-win format — spec is
one page.
*Ref:* `phoboslab/qoi` qoi.h ~670 LOC, **MIT** (reference impl also usable verbatim).

### PNM/PPM — done, `kernel/codecapps/image/pnm.cc`
ASCII header `P1–P6` + maxval + raw/ASCII raster. XS — done.

### AVIF / HEIC — probes only — **future/XL**
Both are ISOBMFF (`ftyp`/`meta`/`iloc`/`mdat`) wrapping an intra video codec:
AV1 for AVIF, HEVC for HEIC. Realistically each = (MP4 demuxer §4.3) + full modern
codec (dav1d-class or HEVC) → multi-month. Keep probes so files identify; decode
returns `Unsupported`. *Refs:* libavif (BSD-2) + dav1d (BSD-2); libde265 (LGPL —
avoid), openhevc.

### Also probed, no decoder: PSD (`8BPS`, PackBits composite — S for flat preview),
JPEG 2000 (EBCOT+wavelet, XL). **APNG**: same probe as PNG, +S for frame compositing.

---

## 3. Audio formats

### WAV/PCM — done, `kernel/codecapps/audio/wav.cc`
`RIFF/WAVE` → `fmt ` chunk `{tag,ch,rate,bits}` + `data` chunk. PCM tag 1 (8/16/24/32
int), tag 3 IEEE float, tag `0xFFFE` extensible (subformat GUID→1/3), tag `0x0055`
MP3-in-WAV → route `data` to `audio_mp3_decode`. Near memcpy → offer zero-copy PcmBuf.
*Effort:* XS (done, extensions S). *Refs:* `dr_wav.h` ~9k LOC PD/MIT-0 if ever needed.

### MP3 — done via shim, `kernel/codecapps/audio/mp3.cc` → `third_party/minimp3`
Frame stream: 4-B header (version/layer/bitrate/SR index/channel mode) → side info
(17/32 B) → bit-reservoir main data → 34 Huffman tables → requant ×4/3 → MS/IS stereo
→ IMDCT (long 36 / short 3×12) + overlap → 32-band polyphase synthesis (~60% of cost).
Skip `ID3v2` via synchsafe size; tolerate `Xing`/`VBRI`.
*Effort:* L (~1.5–2k LOC own-code). *Refs:* **minimp3** single-header ~2.3k LOC,
**CC0** — already integrated; own-code rewrite is code-purity work, not user value.
Alternative **dr_mp3** (minimp3-derived, PD/MIT-0).

### FLAC — done via shim, `kernel/codecapps/audio/flac.cc` → `third_party/dr_libs`
`fLaC` → metadata blocks (`STREAMINFO`, `VORBIS_COMMENT`, `SEEKTABLE`, `PICTURE`) →
frames: subframe header + LPC/FIXED/verbatim residual coded as Rice/Rice2 partitions,
CRC8 header / CRC16 frame. Far simpler than MP3 — best first own-code audio rewrite.
*Effort:* M (~800–1200 LOC). *Refs:* **dr_flac** ~12k LOC PD/MIT-0 (already vendored);
**miniflac** single-file push-API, **0BSD**; spec is **RFC 9639** — clean-room from
RFC is realistic.

### Ogg Vorbis — done via shim, `kernel/codecapps/audio/vorbis.cc` + `ogg.cc` demux
Ogg pages (capture `OggS`, version, flags, granule, seq, CRC, lacing table) → packet
reassembly → 3 Vorbis headers (id/comment/codebooks incl. floor/residue configs) →
floor0/1 + residue decode → MDCT overlap-add. Codebook unpack is the fiddly part.
*Effort:* L (4–6k LOC own). *Refs:* **stb_vorbis.c** 5,704 LOC, PD/MIT — vendored,
worst shim to replace (do last). libvorbis BSD-3 is the normative ref.

### AAC (ADTS) — probe only, propose `kernel/codecapps/audio/aac.cc`
ADTS: 7/9-B header (`FF F1` no-CRC / `FF F9` CRC) + `raw_data_block` frames. AAC-LC:
1024/128-sample MDCT window switching, scalefactor Huffman (11 books), TNS, MS/IS/PNS
stereo tools. HE-AAC adds SBR+PS — out of scope.
*Effort:* L for LC-only. *Feasible:* yes — **miniaac** (single-file C89, allocation-free,
**0BSD**, AAC-LC only) is a direct drop-in answer to "faad2 is too big"; **FAAD2** is
GPL — avoid; **fdk-aac** Fraunhofer license — avoid. Note miniaac skips channel
coupling — fine for first pass.

### Opus — probe only — **future/XL**
`OggS` + `OpusHead`/`OpusTags`; codec = SILK (LPC) + CELT (MDCT) hybrid driven by a
range decoder; RFC 8251 is the spec but it *is* the reference source code, ~50k+ LOC
decoder. No small clean-room implementation exists. Keep probe; XL.

### MIDI SMF — not registered, propose `kernel/codecapps/audio/midi.cc`
`MThd` `{format,ntrks,division}` + N×`MTrk`: VLQ delta-times, channel events with
running status, meta events (tempo `FF 51`, time sig, track name), sysex. The parser is
genuinely S; producing audio needs a synth: square/triangle wave + envelopes (M) or a
SoundFont2 engine (L).
*Refs:* **tml.h** (TinyMidiLoader — merges tracks to a sorted message list, zlib
license) + **tsf.h** (**TinySoundFont**, SF2 synth, ~3.5k LOC, **MIT**, no-stdlib
defines provided); or a 200-line custom FM/PCM bleeper for system sounds.

### MOD / XM / S3M / IT trackers — not registered, propose `kernel/codecapps/audio/mod.cc`
MOD: 31 samples × header, pattern table 64×Ncells packed events `period+sample+effect`,
sample data raw signed; magic `M.K.`/`xCHN` at offset 1080. XM: `Extended Module: `
header + instruments/samples/packed patterns; S3M `SCRM`@44; IT `IMPM`.
Playback = period-table pitch + per-tick effect engine (arpeggio, porta, vibrato,
volume slide, position jump…) + linear-interp channel mixer. Mostly data-shuffling —
very kernel-friendly.
*Effort:* M for MOD+XM. *Refs:* **pocketmod.h** single-file MOD renderer ~1.7k LOC,
**MIT**, zero libc deps — near drop-in; **libxm** (Artefact2) XM/S3M/MOD player,
WTFPL, no allocations; **MilkyTracker's milkyplay** (GPL — reference reading only,
do not vendor); libxmp LGPL — too big.

### Also probed: MP2 (subband filterbank, M — easier than MP3, covers DVD/DVB audio),
AC-3 (ATSC A/52 spec is free; L), ALAC (done — own `alac_codec.cc`, Rice+adaptive
predictor behind MP4 cookie).

---

## 4. Video / containers

### AVI (RIFF) — probe only, propose `kernel/video/avi_demux.cc`
`RIFF/AVI ` → `LIST hdrl` {`avih`, per-stream `LIST strl`{`strh` fourcc-type
`vids`/`auds` + handler, `strf` BITMAPINFOHEADER/WAVEFORMATEX}} → `LIST movi` chunks
`NNdc`/`NNdb`/`NNwb` → `idx1` index; OpenDML `AVIX`/`dmlh` for >4 GiB.
Demux produces a track table + chunk index, then frames route to existing decoders:
`MJPG` → `video_mjpeg_decode`, raw `DIB ` → FrameBuf directly, `##wb` → WAV path.
**This is the path to first real video playback** — MJPEG/DIB content is plentiful.
*Effort:* S–M demux. *Refs:* MS AVI RIFF reference; OpenDML 1.02 pdf; kaitai avi spec.

### MJPEG — done, `kernel/video/videodecoder.cc`
Per-frame = full JPEG baseline decode; inside AVI `##dc` chunks or raw concatenated
`FF D8…FF D9` streams (webcam/motion-jpeg). Cost = JPEG per frame — fine at ≤30 fps
for modest resolutions. Add `video_mjpeg_next(state, FrameBuf&)` pull API.

### Animated GIF as video-lite — still-frame done
GCE delay (cs) + disposal mode + `NETSCAPE2.0` loop → `gif_next_frame(canvas,&delay)`
iterator; composite disposal 1=leave / 2=bg-fill / 3=restore-prev. Same plumbing later
serves APNG (`fcTL` dispose/blend ops). *Effort:* S — best "video" value per LOC.

### MKV / WebM (EBML) — probe only, propose `kernel/video/mkv_demux.cc`
EBML (RFC 8794): big-endian varint IDs/sizes; `Segment`{`Info`,`Tracks`{`TrackEntry`
codec ID string e.g. `V_MPEG4/ISO/AVC`, `A_VORBIS`},`Cluster`{`SimpleBlock`/`BlockGroup`
(lacing Xiph/fixed/EBML)},`Cues`,`SeekHead`}. Matroska RFC 9559; WebM = profile subset.
*Effort:* M demux — worth it once any playable codec exists inside (Vorbis done);
H.264/VP9 payloads stay Unsupported. *Refs:* RFC 9559, webmproject.org container spec.

### MP4 / MOV (ISOBMFF) — probe only, propose `kernel/video/mp4_demux.cc`
Box tree `ftyp moov{mvhd,trak{mdia{mdhd,hdlr,minf{stbl{stsd,stts,stsc,stsz,stco,co64,
ctts,stss}}}}}, mdat`; sample-table gives chunk→sample mapping without touching `mdat`
payloads. Demux is codec-agnostic and immediately useful: `alac`→done, `mp4a`(AAC)→§3.4
miniaac path, `avc1`(H.264)→future. MOV adds `qt  ` brand + `pnot`/`wide` boxes — same
walker. *Effort:* M. *Refs:* ISO 14496-12 (paywalled) but W3C MSE ISOBMFF byte-stream
TR + GPAC wiki suffice.

### MPEG-TS — probe only
188-B packets, `0x47` sync, PAT→PMT→PES→ES demux, adaptation fields. *Effort:* S–M;
value is IPTV/DVB content — defer until a video codec exists.

### YUV4MPEG (y4m) — not registered, propose `kernel/video/y4m.cc`
ASCII header line `YUV4MPEG2 W… H… F…:… I… A… C420jpeg` then per-frame `FRAME\n` +
raw planar YUV. Trivial → XS; doubles as the test-vector format for `videoyuv.cc`
color conversion. *Refs:* mjpegtools yuv4mpeg spec (wiki.linuxquestions / ffmpeg docs).

### H.264 — probe only — **future/XL**
Annex-B NAL stream `00 00 00 01` + SPS/PPS/slice; CAVLC/CABAC entropy, 4×4/16×16 intra
prediction, quarter-pel MC, in-loop deblocking, DPB management. Spec free (ITU-T H.264)
but a conformant decoder is the biggest single item on this page. *Refs:* **h264bsd**
(baseline-profile decoder, ~20k LOC, Nokia Apache-style license) is the smallest
real-world reference; FFmpeg `h264` ~50k+. Keep probe, XL.

### Also probed / noted: MPEG-1/2 video (`videomp2.cc` partial — 16×16 MB, DCT, VLC
tables; finishing = L), FLV tags (S demux), MPEG-PS (S), ASF/WMV (out of scope).

---

## 5. Archives / document-adjacent

New hook namespace: `kernel/codecapps/archive/*.cc` + `CodecKind::Archive` (or extend
`Container`) returning an entry table `{name, offset, size, method}` rather than a
Frame/PcmBuf — a new `codec_archive_list/extract` entry point, mirroring the proposed
`codec_demux` split in `codecs-formats.md` §4.

### ZIP — not registered, propose `kernel/codecapps/archive/zip.cc`
End-of-central-directory record (scan tail for `PK\x05\x06`) → central dir entries
`PK\x01\x02` {name, method, csize, usize, local-hdr offset} → local headers `PK\x03\x04`
→ payload method 0 (stored, memcpy) or 8 (deflate → ZText inflate). Methods 12/14
(bzip2/LZMA) → `Unsupported`. Enables .docx/.odt/.jar/font-pack reads later.
*Effort:* S — the classic easy win once inflate exists. *Refs:* PKWARE APPNOTE.TXT;
**miniz** single-file zlib+ZIP (public domain/MIT, tinfl inflater is a ~550-LOC
coroutine) — we already have own inflate so only the dir-walking logic is needed.

### GZIP + TAR — not registered, propose `kernel/codecapps/archive/{gzip,tar}.cc`
GZIP: `1F 8B` + method 8 + flags (FNAME/FCOMMENT/FHCRC) → raw deflate → CRC32+ISIZE
trailer — S and shares inflate. TAR: 512-B ustar header blocks {name, octal size,
typeflag} + data padded to 512 — XS, pure header walking. Together they cover
`*.tar.gz` payloads — the realistic "how did files get onto this disk" story.
*Refs:* RFC 1952; POSIX ustar; **puff.c** (zlib contrib, ~700 LOC, zlib license) as an
independent inflate cross-check for selftests.

### 7z / XZ — probes suggested, decode L
7z sig `37 7A BC AF 27 1C` → header streams (EncodedHeader, Files, StreamsInfo) —
container parse is M-ish; payloads are usually LZMA/LZMA2 (range coder + LZ77 +
literal context modeling) or BCJ/Delta filters. LZMA decode from scratch is honest L;
the **LZMA SDK ANSI-C decoder is public domain** (LzmaDec.c + 7zTypes.h, ~1.3k LOC,
16 KB state + dictionary, 2–8 KB compiled code) and embeddable. XZ adds a simpler
container (`FD 37 7A 58 5A 00` + CRC) over LZMA2 — **XZ Embedded** is also PD.
*Effort:* LZMA core L, 7z/XZ container M. Not XL — the PD refs keep it tractable.

### TTF / OTF font parsing — partially done, `kernel/font/ttf.cc` + `ttfrast.cc`
sfnt directory → `head/hhea/maxp/cmap/hmtx/loca/glyf` tables → cmap format 4/12 lookup
→ quadratic-outline glyph rasterizer (already renders; Montserrat baked in
`montserrat_ttf_data.cc`). Remaining gaps: OTF/CFF (`OTTO`, PostScript charstrings —
different engine, L), `GPOS` kerning/ligatures (M), TTC index walk (XS).
*Refs:* **stb_truetype.h** 5,321 LOC PD/MIT is the canonical minimal rasterizer;
freetype is far too big; `ttf-parser`/`fontdue` (Rust) for reading only.
WOFF (`wOFF`) = sfnt tables zlib-compressed per-table → XS given inflate.
WOFF2 (`wOF2`) = transformed tables + **brotli** → XL, future.

### PDF — out of scope
`%PDF-` + xref + object streams + content stream interpreter + font machinery.
Text-extract subset M; faithful raster XL. Future.

---

## 6. Top-10 recommended implementation order (value ÷ effort, current state)

1. **GIF animation iterator** — `gif.cc` + disposal/loop compositor → animated
   "video" today. S. Highest ratio on the board.
2. **APNG frames** — same compositing plumbing over `png.cc`; modern browsers emit it.
   S given PNG.
3. **ZIP archive** — `archive/zip.cc`, central-dir + stored/deflate via own inflate.
   S. Unlocks docx/odt/asset packs and is the gateway to everything compressed.
4. **GZIP + TAR** — `archive/{gzip,tar}.cc`. S/XS. Realistic file-transfer story.
5. **AVI demuxer + MJPEG/DIB playback** — `video/avi_demux.cc` → first real `.avi`
   files play using already-done JPEG+BMP paths. S–M.
6. **YUV4MPEG + WAV extensions** — XS each; y4m feeds `videoyuv.cc` test vectors,
   WAV 24/32-bit/float/extensible rounds out the audio baseline.
7. **MIDI SMF parser + tiny synth** — `audio/midi.cc`; tml.h-style parse (S) +
   square-wave/FM synth (M) → system sounds and game music.
8. **MOD/XM player** — pocketmod (MIT) or libxm behind `audio/mod.cc`. S integration,
   M for XM. Big demo-music catalog.
9. **MP4/MOV demuxer** — `video/mp4_demux.cc` (M). Immediately pairs ALAC (done) and
   AAC via **miniaac** (0BSD) → `.m4a` playback; primes H.264 later.
10. **FLAC own-code rewrite** — easiest shim to retire (RFC 9639, Rice+LPC, M).
    Then MP3 own-code (L), Vorbis last (L). Code-purity lane, runs in parallel.

Next wave: WebP-VP8L (M–L), MKV demux (M), MP2 (M), LZMA/7z via LZMA-SDK (L),
MPEG-2 completion (L), AIFF (XS), WAV-ADPCM µ/A-law LUTs (XS), CUR hotspots (XS).

## 7. Honest XL list — mark "future"

| Item | Why XL |
|---|---|
| H.264 | CABAC/CAVLC + intra pred + MC + deblocking + DPB; smallest ref h264bsd ~20k LOC |
| AV1 (→AVIF) | dav1d is ~100k+ LOC; no minimal ref exists |
| HEVC (→HEIC) | bigger than H.264; openhevc/libde265 multi-10k LOC |
| Opus (full) | RFC 8251 spec ≈ libopus ~50k+ LOC; SILK+CELT+range coder |
| JPEG 2000 | EBCOT tier-1 arithmetic coder + wavelets (openjpeg ~60k) |
| WebP `VP8 ` lossy + `ANIM` | a VP8 intra codec ~14k LOC core (Wuffs PR measured it) |
| AAC beyond LC (HE-AAC SBR/PS) | doubles AAC complexity |
| WOFF2 | brotli (2nd-order context modeling) + glyf transform |
| PDF raster / SVG full | content interpreters, not codecs |
| Progressive JPEG / Adam7 | coverage edge cases — keep `Unsupported`, log serial |

All XL items keep probes registered (already done) so files *identify* correctly even
while decode returns `Unsupported`.

---

## 8. Sources

- PNG: <https://www.w3.org/TR/png-3/> (PNG-3 REC, folds APNG in); RFC 2083.
- JPEG: ITU-T T.81 <https://www.w3.org/Graphics/JPEG/itu-t81.pdf>; JFIF
  <https://www.w3.org/Graphics/JPEG/jfif3.pdf>.
  Refs: NanoJPEG <https://keyj.emphy.de/nanojpeg/> (MIT since 1.3.5, ~900 LOC);
  jpeg-compressor/jpgd (PD); stb_image <https://github.com/nothings/stb> (7,798 LOC).
- GIF: <https://www.w3.org/Graphics/GIF/spec-gif89a.txt>.
- BMP/ICO/WAV tags: <https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage>,
  mmreg.h docs; ICO <https://en.wikipedia.org/wiki/ICO_(file_format)>.
- TGA: <https://paulbourke.net/dataformats/tga/>.
- TIFF: TIFF 6.0 + <https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml>.
- QOI: <https://qoiformat.org/qoi-specification.pdf>; qoi.h MIT.
- WebP: **RFC 9649** <https://www.rfc-editor.org/rfc/rfc9649.html>; VP8 RFC 6386;
  Wuffs VP8L <https://github.com/google/wuffs> (Apache-2.0/MIT, PR#168 measured VP8
  lossy at ~14k LOC).
- MP3: <https://github.com/lieff/minimp3> (CC0, single-header ~2.3k LOC); frame doc
  <http://mpgedit.org/mpgedit/mpeg_format/mpeghdr.htm>; ID3v2 <https://id3.org>.
- FLAC: **RFC 9639** <https://www.rfc-editor.org/rfc/rfc9639.html>;
  <https://xiph.org/flac/format.html>; dr_flac ~12k LOC PD/MIT-0
  <https://github.com/mackron/dr_libs>; miniflac 0BSD.
- Vorbis: <https://xiph.org/vorbis/doc/Vorbis_I_spec.html>; Ogg RFC 3533;
  stb_vorbis 5,704 LOC PD/MIT.
- AAC: <https://wiki.multimedia.cx/index.php/ADTS>; ISO 13818-7;
  **miniaac** <https://buffering.party/software/miniaac/> (0BSD, single-file,
  no-allocation AAC-LC); FAAD2 GPL/fdk-aac Fraunhofer — avoid.
- Opus: RFC 8251 (obsoletes 6716); Ogg mapping RFC 7845; libopus BSD-3.
- MIDI: MMA SMF 1.0 mirror <https://midimusic.github.io/tech/midispec.html>;
  TinySoundFont tsf.h MIT + tml.h zlib
  <https://github.com/schellingb/TinySoundFont>.
- Trackers: pocketmod <https://github.com/rombankzero/pocketmod> (MIT);
  libxm <https://github.com/Artefact2/libxm> (WTFPL); libxmp (LGPL, ref only);
  MilkyTracker milkyplay (GPL, ref only).
- AVI: <https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference>;
  OpenDML <https://www.alexander-noe.com/video/documentation/avi.pdf>;
  <https://formats.kaitai.io/avi/>.
- MP4/MOV: <https://www.w3.org/TR/mse-byte-stream-format-isobmff/>; ISO 14496-12;
  GPAC wiki.
- MKV/WebM: **RFC 9559** <https://www.rfc-editor.org/rfc/rfc9559.html>;
  EBML RFC 8794; <https://www.webmproject.org/docs/container/>.
- MPEG-TS/PS: ISO 13818-1; <https://wiki.multimedia.cx/index.php/MPEG-2_Transport_Stream>.
- H.264: ITU-T H.264 (free); h264bsd reference.
- Y4M: mjpegtools/ffmpeg `yuv4mpeg` docs.
- ZIP/GZIP/TAR: PKWARE APPNOTE.TXT; RFC 1952; POSIX ustar;
  miniz <https://github.com/richgel999/miniz> (PD/MIT, tinfl ~550 LOC);
  puff.c (zlib contrib, zlib license).
- 7z/XZ/LZMA: LZMA SDK <https://7-zip.org/sdk.html> (public domain; LzmaDec.c +
  16 KB state + dict, 2–8 KB code); XZ Embedded (PD); 7z-format spec in SDK `DOC/`.
- TTF/OTF/WOFF: <https://learn.microsoft.com/en-us/typography/opentype/spec/>;
  stb_truetype 5,321 LOC PD/MIT; W3C WOFF/WOFF2 RECs; brotli RFC 7932.
- AVIF/HEIC: <https://aomediacodec.github.io/av1-avif/>; ISO/IEC 23008-12; dav1d BSD-2.
- Existing-tree docs: `docs/research/codecs-formats.md` (decoder skeletons, budgets),
  `kernel/codecapps/magic.cc` (probe table + selftests).
