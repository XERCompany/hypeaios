# Codec Formats — Implementation Spec

**Date:** 2025-11-28
**Scope:** Expand `kernel/codecapps` (CodecsApps) to decode the mainstream image, audio,
video and misc formats inside the kernel with our own code — no external libraries.
Bare-metal x86_64, freestanding C++20, no STL. Test harness: QEMU.

## 0. Current state (what is already in the tree)

The registry, magic sniffer and a working decode pipeline **already exist**; this document
is a spec for *filling in the remaining decoders* and for replacing the few third-party
shims with own code. Do not re-design the plumbing.

| Existing piece | Location | Notes |
|---|---|---|
| Magic sniffer + descriptor table | `kernel/codecapps/magic.cc` | `codec_probe(buf,n) -> CodecId`, selftest cases at `magic.cc:313` |
| Codec registry | `kernel/codecapps/registry.cc`, `codec.h` | `codec_register`, `codec_decode`, `codec_encode`, `codec_report()` |
| Scratch allocator | `kernel/codecapps/arena.cc` (`Arena`, `arena_from_pmm`) | all decoders take `Arena &` |
| Decode output types | `kernel/codecapps/codec.h:56-71` | `FrameBuf` (image/video), `PcmBuf` (audio) |
| Own inflate + stored-deflate | `kernel/codecapps/image/inflate.cc` | `inflate_zlib`, `inflate_deflate`; a `kernel/ztext` DEFLATE lib is landing in parallel — assume full inflate+deflate available; when it lands, re-point PNG/zlib users at `kernel/ztext` and keep `image/inflate.cc` as fallback |
| Image decoders (own code) | `kernel/codecapps/image/` | **done:** BMP, PNM, TGA, QOI, PNG, GIF, JPEG-baseline, ICO, TIFF, EXIF |
| Audio decoders | `kernel/codecapps/audio/` | WAV + IMA-ADPCM (own), FLAC (`dr_flac`), MP3 (`minimp3`), Vorbis (`stb_vorbis`), ALAC (own), OGG container parse |
| Video | `kernel/video/` | MJPEG decode, MPEG-2 partial (`videomp2.cc`), RIFF/fourcc demuxer skeleton (`videoemuxer.cc`) |
| BMP writer | `kernel/ui/screenshot.cc` | encode path for screenshots |
| PCM playback path | `kernel/audio/audioapi.h` | `audio_stream_write`, `src_convert` (48 kHz pullmix at `kernel/audio/pullmix.h`) |
| File I/O | `kernel/vfs/hypefs.h:68` | `fs_read(path, buf, cap, &size)` — whole-file reads only |
| Display surfaces | `kernel/xdp/xdp.h` | `xdp_surface` BGRA32; `xdp_op_blit` to composite |

**Stub / missing today** (probe exists, decode = `codec_stub_decode`): PSD, WebP,
JPEG2000, AVIF, HEIC, Opus, MP2, AC3, AAC, MP4, MPEG-2 video, H.264, AVI, TS, MKV.
**No `CodecId` at all yet:** MIDI SMF, APNG (reuses `Png`), CUR (reuses `Ico`), MOV
(reuses `Mp4`), WebM (reuses `Mkv`), WOFF/WOFF2, TTF/OTF, PDF, SVG, AIFF.
**Third-party shims to eventually replace with own code:** `minimp3`, `dr_flac`,
`stb_vorbis` under `third_party/`. Their wrappers already speak the registry ABI, so the
swap is mechanical: same `audio_*_decode(buf,n,out,arena)` signature, own code inside.

Effort scale used below — **XS** <100 LOC, **S** 100–400, **M** 400–1500,
**L** 1.5k–5k, **XL** >5k LOC (decoder only, kernel style).

---

## 1. Inventory — mainstream formats

### 1.1 Image

| Format | Magic bytes | Container structure | Core algorithm | License-safe spec source | Effort | Status | Render target |
|---|---|---|---|---|---|---|---|
| BMP | `BM` | 14-B file header + DIB (`BITMAPINFOHEADER`), palette, rows bottom-up | uncompressed / RLE8 / bitfields | MS Learn "Bitmap Storage" | XS | **done** `image/bmp.cc` | `FrameBuf`→xdp blit |
| PNM (PBM/PGM/PPM) | `P1`–`P6` | ASCII header + raw/binary raster | none | netpbm.sf.net doc | XS | **done** `image/pnm.cc` | `FrameBuf` |
| TGA | footer `TRUEVISION-XFILE.\0` @ −26 | 18-B header (idlen, cmap, type), palette, image ID, raster | raw / RLE packets, 8/16/24/32-bpp, palette | paulbourke.net/dataformats/tga; Wikipedia "Truevision TGA" | XS | **done** `image/tga.cc` | `FrameBuf` |
| QOI | `qoif` | header (w,h,channels,colorspace) + op stream | index/diff/luma/run/RGB(A) ops | qoiformat.org spec (1 page, MIT) | XS | **done** `image/qoi.cc` | `FrameBuf` |
| PNG | `89 50 4E 47 0D 0A 1A 0A` | 8-B sig + chunks `IHDR PLTE tRNS IDAT … IEND`, CRC32 each | zlib/DEFLATE + 5 unfilter modes (+ Adam7) | W3C REC **png-3** (2025); RFC 2083 | M | **done** `image/png.cc` + `inflate.cc` | `FrameBuf` |
| GIF 87a/89a | `GIF87a` / `GIF89a` | header, LSD, GCT; blocks: `2C` image, `21 F9` GCE, `FE` comment, `FF` app, `3B` trailer | LZW (12-bit, clear/EOI codes), sub-blocks, interlace, disposal | w3.org/Graphics/GIF/spec-gif89a.txt | M | **done** `image/gif.cc` | `FrameBuf` (anim→video surface) |
| JPEG (JFIF/EXIF) | `FF D8 FF` | marker stream `SOI APPn DQT SOF0/1/2 DHT DRI SOS RSTn EOI` | baseline: Huffman + dequant + 8×8 IDCT + YCbCr→RGB; progressive = XL | ITU-T T.81 (w3.org mirror); JFIF 1.02 | L | **done** `image/jpeg.cc` (baseline only) | `FrameBuf` |
| ICO / CUR | `00 00 01 00` / `00 00 02 00` | dir header + N×16-B `ICONDIRENTRY` → BMP-DIB or PNG payloads | delegates to BMP/PNG decoder | Wikipedia "ICO"; MSDN ICONDIRENTRY | XS | **done** `image/ico.cc` | `FrameBuf` (cursor/icon) |
| TIFF baseline | `II 2A 00` / `MM 00 2A` | IFD chain, 12-B tag entries, strips/tiles | raw / PackBits / LZW / Deflate; baseline subset | TIFF 6.0 (Adobe, loc.gov mirror); libtiff.org tiff6 | M | **done** `image/tiff.cc` | `FrameBuf` |
| EXIF | `Exif 00 00` or APP1 | TIFF-IFD tag tree inside JPEG APP1 | metadata parse only | CIPA DC-008 / exif.org | S | **done** `image/exif.cc` | tags struct |
| PSD | `8BPS` | header + mode/len sections + layer records + image data | raw / RLE (PackBits) composite | Adobe devnet PSD spec | S (flat) / M (layers) | probe only | `FrameBuf` |
| WebP | `RIFF ???? WEBP` + `VP8 `/`VP8L`/`VP8X` | RIFF chunks: `VP8 ` lossy, `VP8L` lossless, `VP8X`+`ALPH`/`ANIM` | lossless: LZ77+2-level Huffman+color cache+transforms; lossy: VP8 intra | **RFC 9649** (2024); RFC 6386 (VP8) | M (lossless) / L (VP8) | probe only | `FrameBuf` |
| JPEG 2000 | `FF 4F FF 51` or `… 6A 50 20 20` | codestream markers or JP2 boxes | EBCOT, wavelet (CDF 5/3, 9/7), tier-1 arithmetic | ITU-T T.800 | XL | probe only | `FrameBuf` |
| AVIF | `ftyp` + brand `avif`/`avis` | ISOBMFF boxes (`meta`,`iloc`,`mdat`) wrapping AV1 intra | AV1 sequence+OBU decode | aomediacodec.github.io/av1-avif | XL | probe only | `FrameBuf` |
| HEIC | `ftyp` + `heic`/`heix`/`mif1` | ISOBMFF + HEVC intra | HEVC decode | ISO/IEC 23008-12 (paywalled; Nokia HEIF tech doc) | XL | probe only | `FrameBuf` |
| APNG | PNG sig + `acTL` chunk | `acTL`, `fcTL`, `fdAT` interleaved with `IDAT` | PNG pipeline + frame compositing (dispose/blend ops) | W3C png-3 (APNG moved into spec) | S given PNG | none | `videostream`/anim `FrameBuf` |
| SVG | none (text `<?xml`/`<svg`) | XML DOM + viewBox tree | path rasterizer: fills, strokes, transforms, gradients | W3C SVG 1.1 / SVG2 CR | L | none | `FrameBuf`/canvas |
| XCF/ORA/HEIF… | — | — | — | — | — | out of scope | — |

### 1.2 Audio

| Format | Magic bytes | Container structure | Core algorithm | License-safe spec source | Effort | Status | Render target |
|---|---|---|---|---|---|---|---|
| WAV / RIFF | `RIFF ???? WAVE` | `fmt ` + `data` + `LIST` chunks | PCM 8/16/24/32 int, IEEE float; fmt tag 1/3/0xFFFE | MS "Multimedia Data Formats" (sapp.org/doc/WaveFormat mirror) | XS | **done** `audio/wav.cc` | `PcmBuf`→`src_convert`→pullmix |
| WAV-ADPCM (IMA/MS/µ-law) | `RIFF…WAVE` fmt tag `0x11/0x06/0x07` | same RIFF, `fact` chunk | IMA step-table ADPCM 4-bit | same as WAV + IMA ref impl | S | **done** `audio/adpcm.cc` | `PcmBuf` |
| AIFF | `FORM ???? AIFF` | `COMM` + `SSND` chunks (big-endian) | PCM BE | Apple AIFF 1.3 (mirrors) | XS | none | `PcmBuf` |
| MP3 (MPEG-1/2/2.5 L3) | `FF Ex` sync or `ID3` | frame stream: 4-B hdr + side info + main data; `Xing`/`VBRI`/`ID3v2` | Huffman granule decode, requant ×4/3, stereo MS/IS, IMDCT+window, 32-tap polyphase synth | ISO 11172-3 (paywalled); mpgedit.org header doc; ID3v2 at id3.org | L (~1.5–2k LOC) | shim `minimp3` — **own-code rewrite pending** | `PcmBuf` |
| MP2 (MPEG-1 L2) | `FF Fx` sync (layer bits `10`) | frame stream, simpler than L3 | subband filterbank + fixed/quant tables, no Huffman reservoir | ISO 11172-3; DAB refs | M | probe only | `PcmBuf` |
| FLAC | `fLaC` (or Ogg-FLAC `OggS…7F FLAC`) | metadata blocks (`STREAMINFO`, `VORBIS_COMMENT`, `SEEKTABLE`) + frames | LPC/FIXED/verbatim subframes + Rice/Rice2 residual, CRC8/16 | **RFC 9639**; xiph.org flac format.html | M (~800–1200 LOC) | shim `dr_flac` — rewrite feasible | `PcmBuf` |
| Ogg Vorbis | `OggS` + `\x01vorbis` | Ogg pages (lacing, CRC) → 3 header packets → audio packets | floor0/1+residue codebooks, MDCT (Vorbis I) | xiph.org Vorbis_I_spec.html; RFC 3533 (Ogg) | L (4–6k) | shim `stb_vorbis`; Ogg demux **done** `audio/ogg.cc` | `PcmBuf` |
| Ogg Opus | `OggS` + `OpusHead` | Ogg pages, `OpusHead`/`OpusTags` | SILK + CELT hybrid, range decoder, MDCT | **RFC 6716 → RFC 8251**; RFC 7845 (Ogg mapping) | XL (reference is huge; NB subset = L) | probe only | `PcmBuf` |
| AAC (ADTS) | `FF F1`/`FF F9` sync | ADTS fixed/var hdr + raw_data_block frames | AAC-LC: MDCT 1024/128, TNS, scalefactor Huffman (11 tables), PNS/MS/IS | ISO 13818-7 (paywalled); wiki.multimedia.cx ADTS; FAAD2 ref | L | probe only | `PcmBuf` |
| AC-3 | `0B 77` sync | syncframe: bsi + 6 coded audio blocks | MDCT, bit allocation, exponent strategy | ATSC A/52 (free, atsc.org) | L | probe only | `PcmBuf` |
| MIDI SMF | `MThd` | `MThd` header + N×`MTrk` events, running status, VLQ deltas, meta/sysex | event parser (S) + wavetable/FM synthesizer (L) | MMA "Standard MIDI Files 1.0" RP-001; midimusic.github.io mirror | S parse / L synth | none | synth→`PcmBuf`→pullmix |
| ALAC | `ftyp` brand `alac`/`M4A ` | ISOBMFF + `alac` magic cookie | Rice + adaptive FIR prediction | Apple opensource ALAC (Apache-2.0) | M | **done** `audio/alac_codec.cc` | `PcmBuf` |
| MOD/S3M/IT/XM | `M.K.`/`SCRM`/`IMPM`/`Extended Module:` | pattern/sample tables | tracker mixer (period tables, effects) | modplug specs (open) | M | none | `PcmBuf` |

### 1.3 Video / containers

| Format | Magic bytes | Container structure | Core algorithm | License-safe spec source | Effort | Status | Render target |
|---|---|---|---|---|---|---|---|
| AVI (RIFF) | `RIFF ???? AVI ` | `LIST hdrl{avih, LIST strl{strh,strf}}`, `LIST movi{##db,##dc,##wb}`, `idx1`; OpenDML `AVIX`/`dmlh` | demux only; per-stream fourcc→CodecId | MS Learn "AVI RIFF File Reference"; OpenDML 1.02 pdf | S (parse) | probe + fourcc map in `videoemuxer.cc` | track table → `videostream` |
| Animated GIF | `GIF89a` + `21 F9` GCEs | image blocks + NETSCAPE2.0 loop ext | GIF decoder + timing/disposal compositor | spec-gif89a.txt | S given GIF | still-frame done | anim `FrameBuf` seq |
| APNG | PNG + `acTL` | see §1.1 | PNG + compositing | W3C png-3 | S given PNG | none | anim `FrameBuf` seq |
| MP4 / MOV (ISOBMFF) | `???? ftyp` brands `isom/iso2/mp41/mp42/qt  /avc1/M4A ` | box tree: `ftyp moov{mvhd,trak{mdia{mdhd,hdlr,minf{stbl{stsd,stts,stsc,stsz,stco,co64,ctts,stss}}}}}, mdat` | demux + sample table index; codec-agnostic | ISO 14496-12 (paywalled); W3C MSE ISOBMFF byte-stream TR; GPAC wiki | M | probe only | track table |
| MKV / WebM (EBML) | `1A 45 DF A3` (+ `DocType`=`matroska`/`webm`) | EBML elements: `Segment{Info,Tracks,Cluster{Block},Cues,SeekHead}` | varint EBML IDs/sizes, lacing (Xiph/fixed/EBML) | **RFC 9559** (Matroska), RFC 8794 (EBML); webmproject.org container | M | probe only | track table |
| MPEG-TS | `47` sync @ 0,188,376 | 188-B packets, PAT/PMT tables, PES | PSI demux + PES reassembly | ISO 13818-1; wiki.multimedia.cx MPEG-TS | S–M | probe only | track table |
| MJPEG | per-frame `FF D8 FF` | AVI `##dc` chunks or raw stream | = JPEG baseline per frame | T.81 + AVI ref | M given JPEG | **done** `video/videodecoder.cc` | `VideoFrame` |
| MPEG-1/2 video | `00 00 01 B3` seq hdr | start-code stream: seq/GOP/picture/slice/macroblock | 16×16 MBs, DCT, motion comp, VLC tables | ISO 11172-2 / 13818-2 (paywalled); mpeg.org refs | L | partial `videomp2.cc` | `VideoFrame` |
| H.264 | `00 00 00 01` NAL SPS `6x` | NAL units (Annex-B) or `avcC` | CAVLC/CABAC, 4×4/16×16 intra pred, inter MC, deblocking | ITU-T H.264 (free from itu.int) | XL | probe only | `VideoFrame` |
| FLV | `46 4C 56` | tags: audio/video/script, prev-size | demux only | Adobe FLV spec (open) | S | none | track table |
| RM/WMV/ASF | `2E 52 4D 46` / ASF GUID | proprietary trees | — | MSDN ASF spec | L | none | out of scope |

### 1.4 Misc

| Format | Magic bytes | Container structure | Core algorithm | Spec source | Effort | Status | Render target |
|---|---|---|---|---|---|---|---|
| TTF / OTF | `00 01 00 00` / `true` / `OTTO` / `ttcf` | sfnt table directory: `glyf`, `head`, `hmtx`, `cmap`, `kern`, `GPOS` | cmap lookup + TrueType rasterizer (quadratic outlines) | MS OpenType spec (free) | L (rasterizer done?) | `kernel/font/` already renders | glyph cache→text layer |
| WOFF | `wOFF` | 44-B hdr + table dir, each table zlib/stored | zlib inflate → sfnt passthrough | W3C REC WOFF 1.0 | XS given inflate | none | same as TTF |
| WOFF2 | `wOF2` | transform'd tables + **brotli** stream | brotli decode (LZ77+2nd-order context modeling) + glyf transform | W3C REC WOFF2; RFC 7932 (brotli) | XL | none | same as TTF |
| PDF | `%PDF-` | xref tables/streams, objects, pages tree, content streams | text-out only = M (extract Tj/TJ, fonts); full render = XL | ISO 32000-2; Adobe PDF Ref 1.7 (free pdf) | M / XL | none | text→surface or page raster |
| ZIP (for docx/odt later) | `PK\x03\x04` | local headers + central dir | DEFLATE stored/deflated | PKWARE appnote.txt | XS given ztext | — | archive listing |
| PostScript/EPS | `%!PS` | interpreted language | full interpreter | Adobe PLRM | XL | none | out of scope |

---

## 2. Implementation order — top-8 by value ÷ effort

The requested ranking was: PNG, GIF, JPEG-baseline, ICO, TGA, WAV, MP3, AVI-parse.
That ranking still stands on value/effort grounds; the honest status column shows most of
it is already implemented, so each entry below says what is *left* rather than pretending
the work is greenfield.

| # | Format | Value | Effort | Ratio | Remaining work |
|---|---|---|---|---|---|
| 1 | **WAV** | every test clip, simplest audio path to the mixer | XS | highest | done; add 24/32-bit + float + `WAVE_FORMAT_EXTENSIBLE` |
| 2 | **TGA** | icons/test images, ~150 LOC | XS | highest img | done |
| 3 | **ICO/CUR** | desktop needs icons/cursors now | XS (delegates to BMP/PNG) | very high | done; add CUR hotspot → `kernel/input/cursprite.cc` |
| 4 | **PNG** | the standard lossless image; screenshots, icons, docs | M (inflate exists) | high | done; add Adam7 interlace + APNG chunks |
| 5 | **GIF** | every old-web asset; doubles as first "video" | M | high | done single-frame; add frame iterator + disposal/loop for anim playback |
| 6 | **JPEG baseline** | all camera/web photos | L | medium-high | done (`jpeg.cc`, NanoJPEG-derived, public-domain provenance); add restart-marker robustness, grayscale 4:0:0, EXIF orientation via `exif.cc` |
| 7 | **MP3** | most common music format | L | medium | working via `minimp3` shim; **own-code rewrite** is the real task (see §3.7 — honest effort L, ~2k LOC) |
| 8 | **AVI-parse** | only mainstream container we can actually fill (MJPEG/MPEG-2 + WAV inside) | S | medium-high | probe + fourcc table exist (`videoemuxer.cc`); need `hdrl/movi/idx1` walker → track table, then hand chunks to `video_mjpeg_decode` / `audio_wav_decode` |

**Next wave after top-8** (also ranked by value ÷ effort):
9. MP4/MOV demuxer (M) — unlocks M4A/AAC and H.264 *later*; 10. FLAC own-code (M — much
easier than MP3: Rice+LPC, replaces `dr_flac`); 11. MKV/WebM demuxer (M); 12. MIDI SMF
parser (S) + simple square-wave/FM synth (M) for system sounds; 13. WebP-lossless `VP8L`
(M — shares Huffman/LZ77 machinery with inflate); 14. APNG/anim-GIF playback glue (S);
15. WOFF (XS — zlib→sfnt); 16. AAC-LC ADTS (L) once MP4 exists; 17. MP2 (M, easier than
MP3, covers DVB/DVD audio); 18. Vorbis own-code (L — replace `stb_vorbis` last, it's the
most complex audio decoder we ship). Deferred XL: H.264, AV1/AVIF, HEIC, Opus full,
JPEG2000, WOFF2/brotli, PDF render, PSD layers, SVG full.

---

## 3. Decoder skeleton specs (per chosen format)

File layout (matches existing tree — task suggestion `kernel/codecs/{img,aud,vid}` maps
onto what we already have):

- `kernel/codecapps/image/` — image decoders (`png.cc`, `gif.cc`, …)
- `kernel/codecapps/audio/` — audio decoders
- `kernel/video/` — demuxers + video codecs (`videoemuxer.cc`, `videodecoder.cc`)
- `kernel/codecapps/magic.cc` — probes must be added/updated for every new format
- `kernel/codecapps/registry.cc` / descriptor table in `magic.cc` — register each decoder

ABI every decoder implements (from `codec.h`):

```cpp
CodecStatus fmt_decode(const hcpp::u8* in, hcpp::size_t in_size,
                       void* out /*FrameBuf*|PcmBuf*/, Arena& arena) noexcept;
```

### 3.1 PNG — `image/png.cc` (done; spec for reference/extension)

- Parse 8-B signature → chunk loop `(u32 len, u32 type, data, u32 crc32_be)`; verify CRC
  (`hcpp/crc32` exists); collect `IHDR{w,h,bitdepth,colortype,interlace}`, `PLTE`,
  `tRNS`, concat all `IDAT`; stop at `IEND`.
- `IDAT` payload → `inflate_zlib()` (own, `inflate.cc`) → filtered scanlines.
- Scanline = `1 + ceil(w * bitdepth * channels / 8)`; filter byte per row; unfilter modes:
  **0** None, **1** Sub (`+ recon[x-bpp]`), **2** Up (`+ prior[x]`),
  **3** Average (`+ (a+b)/2`), **4** Paeth (`p=a+b−c`, pick nearer of a,b,c).
- Color types: 0 gray, 2 RGB, 3 palette, 4 gray+α, 6 RGBA; bit depths 1/2/4/8/16 —
  unpack sub-byte samples, expand all to RGBA32 out.
- Interlace: Adam7 = 7 passes with origins/strides `(0,0,8,8) (4,0,8,8) (0,4,4,8)
  (2,0,4,4) (0,2,2,4) (1,0,2,2) (0,1,1,2)` — currently can return `Unsupported` for
  `interlace=1`; add as pass-loop over same unfilter core.
- APNG: `acTL` (num frames/loops), `fcTL` (rect, delay, dispose_op, blend_op), `fdAT`
  (IDAT minus seq number). Same inflate path per frame; compose on full canvas.
- Budget: `inflated ≈ h*(1+w*4)` + `out = w*h*4` → 1080p RGBA ≈ 6.3 MB + 8.3 MB; arena
  16 MB headroom, or stream rows into `FrameBuf` once `kernel/ztext` supports pull.

### 3.2 GIF — `image/gif.cc` (done single frame; animation spec)

- Header `GIF8[79]a`; LSD: `w,h,packed{gct_flag,color_res,sort,gct_pow2},bg,aspect`;
  GCT `3 * 2^(gct+1)` bytes.
- Block loop: `0x2C` Image Descriptor `{l,t,w,h,packed{lct_flag,interlace,lct_pow2}}`
  → LZW data sub-blocks; `0x21` ext: `F9` GCE `{packed{disposal,user,trans_flag},
  delay_ms/10, trans_idx}`, `FE` comment, `FF` app (`NETSCAPE2.0` → loop count);
  `0x3B` trailer.
- LZW: `min_code_size` 2–8; `clear = 1<<mcs`, `eoi = clear+1`; codes grow to 12 bits;
  dictionary: prefix/suffix stacks (stack ≤ 4K, prefix ≤ 4K × u16); emit per pixel.
- Interlace row order: pass strides `(0,8),(4,8),(2,4),(1,2)`.
- Animation API: `gif_next_frame(state, FrameBuf& canvas, u32& delay_ms)` — apply
  disposal **before** next frame: 1 = leave, 2 = fill rect with bg color,
  3 = restore saved previous canvas. This is the piece to add for anim playback.
- Budget: canvas `w*h` + saved `w*h` + palettes 1.5 KB + LZW dict ~70 KB.

### 3.3 JPEG baseline — `image/jpeg.cc` (done; spec + hardening list)

- Marker walk: `FF D8` SOI → segments `FF Mn, u16 len` until `FF DA` SOS. Handle
  `APP0` JFIF, `APP1` EXIF, `DQT` (≤4 tables, 8/16-bit), `SOF0`/`SOF1` baseline
  (reject `SOF2` progressive → `Unsupported`), `DHT` (≤4 DC + ≤4 AC), `DRI` restart
  interval, `RSTn` resync, `FF 00` stuffed bytes.
- Per component: `id, h/v sampling (1|2), qtable`. Support combos 1×1/2×1/1×2/2×2
  (4:4:4, 4:2:2, 4:1:1, 4:2:0) and single-component gray.
- MCU loop over `(ceil(w/8h_max), ceil(h/8v_max))`; per 8×8 block: Huffman DC diff,
  AC RLE `(run,size)` pairs, ZRL/EOB, zigzag reorder, dequantize, fixed-point IDCT
  (Loeffler/AAN — `icos` table already in file), level-shift +128.
- Chroma upsample nearest (or box for 2×2), YCbCr→RGB:
  `R=Y+1.402Cr', G=Y−0.344Cb'−0.714Cr', B=Y+1.772Cb'` (fixed-point, /256).
- Hardening: enforce restart markers (`DC pred reset`); bound `h*v ≤ 10` total blocks
  per MCU; tolerate missing EOI; expose `jpeg_info()` for dims-before-decode
  (thumbnail path).
- Budget: coef planes `≈ w*h*(1+chroma sub)` + RGBA out `w*h*4` ≈ 11 MB @1080p; Huffman
  tables ~1.5 KB; IDCT temp 64×i32.

### 3.4 ICO/CUR — `image/ico.cc` (done; CUR spec)

- DIR header: `u16 reserved(0), u16 type(1=ico,2=cur), u16 count`; then
  `count × ICONDIRENTRY {w8,h8 (0=256), colors, rsv, planes_or_hotx, bpp_or_hoty,
  u32 size, u32 offset}`.
- Payload dispatch: if bytes at offset start with PNG sig → `png_decode`; else
  BMP-DIB (`BITMAPINFOHEADER`, height = 2×icon_h: XOR bitmap then 1-bit AND mask —
  expand AND to alpha).
- Pick best entry: caller supplies `want_size` (e.g. 16/32/48); else largest.
- CUR: entry `planes/hotx`+`bpp/hoty` are **hotspot x/y** → return in a
  `struct CursorImage { FrameBuf fb; u16 hot_x, hot_y; }` for `cursprite.cc`.
- Budget: ≤ 256×256×4 = 256 KB worst case.

### 3.5 TGA — `image/tga.cc` (done; spec)

- 18-B header `idlen, cmaptype, imgtype, cmap{first,len,bpp}, x,y,w,h,bpp,desc`.
- Types: 1/9 palette, 2/10 truecolor, 3/11 gray; 9/10/11 = RLE packets
  (`hdr: count-1 | run<<7`).
- `desc` bit5 = top-left origin (else flip rows), bit4 = right-to-left, low 4 bits =
  attribute bits (treat ≥1 as alpha).
- Budget: `w*h*4`; footer sniff for probe: last 26 B `TRUEVISION-XFILE.\0` —
  probe function must check file *tail*, so `codec_probe_file` (§4) reads head 64 B
  **and** tail 32 B.

### 3.6 WAV — `audio/wav.cc` (done; extension list)

- `RIFF u32sz WAVE`, chunks: `fmt {tag, ch, rate, byterate, align, bits[, cbsize]}`,
  `data`, `fact`, `LIST INFO`, `cue `.
- Extend: tags `0x0001` PCM (add 24/32-bit unpack), `0x0003` float, `0xFFFE` extensible
  (subformat GUID → map to 1/3), `0x0011` IMA-ADPCM (done), `0x0006/7` A/µ-law
  (XS — 256-entry LUT or 10-line expander), `0x0055` MP3-in-WAV → route `data` to
  `audio_mp3_decode`.
- Budget: PCM is memcpy of `data` chunk — offer **zero-copy** variant returning a
  `PcmBuf` whose `data` aliases the input (file buffer outlives playback) to avoid a
  2× memory bill on big tracks.

### 3.7 MP3 — `audio/mp3.cc` (own-code rewrite; honest effort **L, ~1500–2000 LOC**)

Current `mp3.cc` wraps `third_party/minimp3` (CC0) behind the registry ABI. An own-code
decoder is minimp3-class scope. Skeleton:

1. **Sync + header** (already partially in tree): scan `FF Ex`; parse version
   (MPEG1/2/2.5), layer (`01` = Layer III), bitrate/sample-rate index tables,
   padding, channel mode (stereo/joint/dual/mono). Skip `ID3v2` via synchsafe size
   (already done, `mp3.cc:9`); tolerate `Xing`/`VBRI` headers (frame count/TOC).
2. **Side info** (17 B mono / 32 B stereo @MPEG1; 9/17 @MPEG2/2.5): `main_data_begin`
   (bit-reservoir backpointer), private bits, per granule×channel `part2_3_length`,
   `big_values`, `global_gain`, scalefac selectors, `win_switch`, block type, mixed
   flag, `table_select[3]`, region gains, `scalefac_scale`, `count1table_select`.
3. **Main data**: reservoir ring buffer (≤ 511 B back-reference); Huffman layer-1
   decode with the 34 static tables (linbits, x/y pairs), `count1` quadruples table.
4. **Requantization**: `x = sign * |v|^(4/3) * 2^(gain)` (pow4/3 table or NR iteration);
   scalefactors per band; stereo intensity/MS matrixing; alias reduction
   (8 butterflies).
5. **IMDCT**: 36-point long (3 window shapes + short 3×12); overlap-add halves.
6. **Synthesis polyphase**: 32 subbands × 18 samples, DCT-IV + 512-tap window —
   ~60 % of decode cost; must be table-driven fixed/float.
7. Output interleaved s16, resample via existing `src_convert` to 48 kHz.

Budget: static tables ~60 KB `.rodata`; reservoir 2×~2.5 KB; PCM out ~ 10 MB per
5-min stereo track (PcmBuf). **Fallback plan:** keep `minimp3` permanently and mark
own-code MP3 as a "pride project" — value already delivered by the shim; effort is the
highest in the top-8.

### 3.8 AVI parse — `video/avi_demux.cc` (new; reuse `videoemuxer.cc` fourcc tables)

- `RIFF u32sz 'AVI '` → chunks: `LIST 'hdrl'`{`avih` (µs/frame, totalframes, streams,
  w,h); per-stream `LIST 'strl'`{`strh` (fccType `vids`/`auds`, fccHandler, scale,
  rate, length, rcFrame), `strf` (`BITMAPINFOHEADER` for vids — w,h,bpp,codec fourcc;
  `WAVEFORMATEX` for auds)}}; `LIST 'movi'` chunks `NNdc`/`NNdb` (video frames,
  compressed/raw) and `NNwb` (audio); `idx1` index `{ckid, flags, offset, size}`;
  OpenDML `LIST 'AVIX'` continuation + `dmlh` real frame count.
- Output struct: `AviInfo {u32 w,h, fps_us, frames; Track v{codec:CodecId, fourcc};
  Track a{format_tag, rate, ch}; ChunkIndex idx[]}` — build idx from `idx1` or scan
  `movi` once (bounded by file size).
- Frame pull: `avi_read_frame(i)` → `##dc` buffer → `video_mjpeg_decode` (MJPG fourcc)
  or raw DIB → `FrameBuf`; audio `##wb` concat → `audio_wav_decode`/adpcm.
- **No AVI `CodecId` decode fn needed**: register an `avi_probe` → `CodecId::Avi`
  returning a `ContainerInfo` from a new `codec_demux(path)` entry point (§4) rather
  than `codec_decode` (containers produce tracks, not a single buffer).
- Budget: index ≈ `frames × 16 B` (~50 KB/hour of 30 fps); per-frame decode = JPEG
  budget above.

---

## 4. Registry & unified entry points

Existing (keep): `codec_probe(buf,n)→CodecId`, `codec_decode(id,in,n,out,arena)`,
`codec_register_builtins()`, `codec_report()` serial dump, selftests
(`codec_probe_selftest`, `codec_arena_selftest`, per-format `*_selftest`).

**Probe-order contract** (document in `magic.cc`): specific-before-generic —
`adpcm` before `wav` (both match `RIFF…WAVE`), `flac/vorbis/opus` before `ogg`,
`alac/avif/heic` before `mp4` (all match `ftyp`), `mp2/mp3/aac/ac3` before nothing
(distinct sync patterns). New probes must be inserted respecting this or added to
`probe_cases` selftest.

Proposed additions (new file `kernel/codecapps/fileio.cc`):

```cpp
namespace hype::codec {
    // Probe a file: reads first 64 B (and last 32 B for TGA footer) via fs_read.
    CodecId codec_probe_file(const char* path) noexcept;

    // Unified image entry: fs_stat -> fs_read whole file -> probe -> decode.
    // FrameBuf pixels are BGRA32 for xdp blit; ownership = PMM arena, caller
    // frees via img_release().
    bool img_decode_file(const char* path, FrameBuf& out) noexcept;
    bool img_decode_thumb(const char* path, hcpp::u32 max_w, hcpp::u32 max_h,
                          FrameBuf& out) noexcept;      // decode + box downscale
    void img_release(FrameBuf&) noexcept;

    // Unified audio entry: decode -> PcmBuf, then caller does
    // src_convert(rate->48000) + audio_stream_write(stream, ...).
    bool aud_decode_file(const char* path, PcmBuf& out) noexcept;

    // Containers (AVI/MP4/MKV/TS/Ogg): returns track table + index, not pixels.
    bool codec_demux_file(const char* path, DemuxInfo& out) noexcept;
}
```

File-size guard: refuse `fs_read` of files > `codec_file_max` (suggest 64 MiB image /
256 MiB audio) — VFS is whole-file anyway; streaming is later work. All decode arenas
come from `arena_from_pmm()` pages sized by the §3 budgets; `img_release` hands pages
back via `arena_release_pmm`.

Thumbnails for `hxfiles` (`kernel/hxrt/apps/hxfiles.cc`): `img_decode_thumb` +
a tiny `frame_downscale(src,dst)` (box filter) — defer UI wiring to the hxfiles task;
cache thumbs under `U:/.thumbs/` keyed by path hash once fs is writable there.

Selftests stay QEMU-serial-verifiable: embed a 2×2 PNG/WAV/GIF blob per new decoder
(pattern follows `png_selftest_data.h`, `jpeg_selftest_blob.h`).

---

## 5. PMM / buffer budget summary

| Decoder | Static tables | Working set (1080p / 5-min stereo) | Suggested arena |
|---|---|---|---|
| PNG | ~1 KB | inflated ~6.3 MB + out 8.3 MB | 16 MB |
| JPEG | ~2 KB | coef ~3 MB + out 8.3 MB | 12 MB |
| GIF anim | ~70 KB | 2×canvas idx ~4 MB + out 8.3 MB | 12 MB |
| ICO | — | ≤256 KB | 512 KB |
| TGA | — | out 8.3 MB | 8.5 MB |
| WAV | — | = file size (or 0 with zero-copy) | n/a |
| MP3 own | ~60 KB | PCM ~26 MB (5 min) | 32 MB or chunked PcmBuf |
| AVI demux | — | index ~50 KB + per-frame JPEG | 12 MB while playing |
| WebP `VP8L` | ~10 KB | bit-image + out ≈ 12 MB | 16 MB |
| FLAC own | ~4 KB | PCM ~50 MB worst (24/96 stereo) | chunked or 64 MB cap |

---

## 6. Sources

- PNG: <https://www.w3.org/TR/png-3/> (W3C REC, 2025-06-24; includes APNG);
  RFC 2083 <https://www.rfc-editor.org/rfc/rfc2083>
- GIF: <https://www.w3.org/Graphics/GIF/spec-gif89a.txt>
- JPEG: ITU-T T.81 mirror <https://www.w3.org/Graphics/JPEG/itu-t81.pdf>;
  JFIF 1.02 <https://www.w3.org/Graphics/JPEG/jfif3.pdf>
- BMP: <https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage>
- ICO/CUR: <https://en.wikipedia.org/wiki/ICO_(file_format)>
- TGA: <https://paulbourke.net/dataformats/tga/>; <https://en.wikipedia.org/wiki/Truevision_TGA>
- TIFF: <https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml>;
  TIFF 6.0 pdf (Adobe)
- QOI: <https://qoiformat.org/qoi-specification.pdf>
- WebP: **RFC 9649** <https://www.rfc-editor.org/rfc/rfc9649.html>;
  VP8 bitstream RFC 6386
- AVIF: <https://aomediacodec.github.io/av1-avif/> ; HEIC: ISO/IEC 23008-12;
  JPEG2000: ITU-T T.800
- SVG: <https://www.w3.org/TR/SVG11/>
- WAV/AIFF: <https://soundfile.sapp.org/doc/WaveFormat/>; MS mmreg.h tags
  <https://learn.microsoft.com/en-us/windows/win32/api/mmreg/>
- MP3: header/frame doc <http://mpgedit.org/mpgedit/mpeg_format/mpeghdr.htm>;
  ISO/IEC 11172-3; ID3v2 <https://id3.org/id3v2.4.0-structure>; reference impl
  <https://github.com/lieff/minimp3> (CC0)
- FLAC: **RFC 9639** <https://www.rfc-editor.org/rfc/rfc9639.html>;
  <https://xiph.org/flac/format.html>
- Ogg: RFC 3533; Vorbis I spec <https://xiph.org/vorbis/doc/Vorbis_I_spec.html>
- Opus: RFC 8251 (obsoletes 6716); Ogg mapping RFC 7845
- AAC ADTS: <https://wiki.multimedia.cx/index.php/ADTS>; ISO/IEC 13818-7
- AC-3: ATSC A/52 <https://www.atsc.org/standard/a52-atsc-digital-audio-compression-standard-ac-3-e-ac-3/>
- MIDI SMF: MMA RP-001 "Standard MIDI Files 1.0", mirror
  <https://midimusic.github.io/tech/midispec.html>
- AVI: <https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference>;
  OpenDML pdf <https://www.alexander-noe.com/video/documentation/avi.pdf>;
  Kaitai reference <https://formats.kaitai.io/avi/>
- MP4/ISOBMFF: W3C byte-stream TR
  <https://www.w3.org/TR/mse-byte-stream-format-isobmff/>; ISO 14496-12; GPAC wiki
- MKV/WebM: **RFC 9559** <https://www.rfc-editor.org/rfc/rfc9559.html>;
  EBML RFC 8794; <https://www.webmproject.org/docs/container/>
- MPEG-TS: ISO 13818-1; <https://wiki.multimedia.cx/index.php/MPEG-2_Transport_Stream>
- TTF/OTF: <https://learn.microsoft.com/en-us/typography/opentype/spec/>
- WOFF/WOFF2: <https://www.w3.org/TR/WOFF/> , <https://www.w3.org/TR/WOFF2/>;
  brotli RFC 7932
- PDF: Adobe PDF Reference 1.7 <https://opensource.adobe.com/dc-acrobat-sdk-docs/>;
  ISO 32000-2
- PSD: <https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/>

## 7. Honest risks / non-goals

- **XL items stay stubs for now**: H.264, AV1/HEVC (AVIF/HEIC), Opus-full, JPEG2000,
  WOFF2/brotli, PDF raster — each is a multi-month project; register probes so files
  identify correctly but decode returns `Unsupported`.
- Third-party shims (`minimp3`, `dr_flac`, `stb_vorbis`) are CC0/0BSD/MIT-compatible
  and already behind the registry ABI; replacing them is code-purity work, not user
  value — do FLAC first (easiest), MP3 when the L-effort lands, Vorbis last.
- Progressive JPEG, Adam7 PNG and interlaced GIF are coverage edge cases — acceptable
  to return `Unsupported` initially; log to serial for the selftest corpus.
- All decoders are whole-file today; true streaming (chunked `fs_read`, pull-inflate)
  waits on `kernel/ztext` pull API and a block-level VFS read path.
