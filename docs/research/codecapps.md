# CodecApps research — image/audio/video codecs for hypeaios

Gathered 2026-11-26 (subagent research). Video codecs deferred to a separate program per user decision.

## Image formats

| Format | Magic | Decode difficulty | Freestanding verdict |
|---|---|---|---|
| BMP | `BM`, DIB @0x0E | Trivial | Yes — palette/24/32bpp |
| PNM/PPM/PGM | `P1`–`P6` | Trivial | ~100 LOC |
| TGA | footer `TRUEVISION-XFILE` | Trivial | raw/RLE/palette |
| QOI | `qoif` | Trivial | ~300 LOC, spec 1 page (`phoboslab/qoi`, MIT) |
| PNG | `89 50 4E 47...` | Medium — inflate + unfilter; `hcpp/crc32` exists | Port puff/tinf (~500 LOC) or own inflate; lodepng alternative |
| GIF | `GIF87a/89a` | Medium — LZW ~200 LOC | Doable |
| JPEG | `FF D8 FF` | Hard — Huffman/dequant/IDCT ~1.5-3k LOC | Port stb_image JPEG or baseline-only |
| ICO/CUR | `00 00 01/02 00` | Trivial | wraps BMP/PNG |
| TIFF | `II*\0`/`MM\0*` | Medium | baseline (raw/PackBits/LZW) |
| WebP | `RIFF`+`WEBP` | Hard — VP8/VP8L | defer |
| JPEG2000 | `FF 4F FF 51` | Very hard | skip |
| AVIF/HEIC | `ftyp` | Very hard (HEIF+AV1/HEVC) | skip |
| PSD | `8BPS` | Medium | composite RLE/raw ok |
| RAW/DNG | TIFF magic + tag 50706 | Hard | concept only |
| SVG | `<?xml` | Hard | vector, note only (nanosvg ~4k) |
| EXIF | `Exif\0\0` TIFF | Medium | cheap metadata reader |

Encode realistic: BMP/PNM/TGA/QOI trivial; PNG-stored-deflate trivial (legal PNG), fixed-Huffman ~300 LOC; JPEG enc (jo_jpeg ~300/stb ~700 LOC); GIF enc medium.

## Audio codecs

| Codec | Magic | Complexity | Path |
|---|---|---|---|
| WAV | `RIFF`+`WAVE` | Trivial | done (`audioparse`) |
| ADPCM/G.711 | WAV tag 2/6/7 | Trivial | table decode |
| FLAC | `fLaC` / `OggS`+`\x7FFLAC` | Medium | **dr_flac** (PD/MIT-0, alloc callbacks) |
| MP3 | `FF FB` sync/`ID3` | Med-hard | **minimp3** (CC0, header-only, `MINIMP3_NO_SIMD`, no malloc) |
| Vorbis | `OggS`+`\x01vorbis` | Hard | **stb_vorbis** (5.7k LOC, PD, no-malloc mode) |
| Opus | `OggS`+`OpusHead` | Very hard | libopus ~60k — defer |
| AAC | ADTS `FF F1` / MP4 `mp4a` | Hard | FAAD2/fdk-aac libc-heavy — defer (AAC-LC patents expired ~2017 anyway) |
| ALAC | MP4 `alac` | Medium | ref impl Apache-2.0 ~1.5k — skeleton exists |
| AC3 | `0B 77` | Hard | defer |
| DTS/WMA/AMR | — | Hard | note only |
| MIDI | `MThd` | n/a | synth, not codec; stretch |

Containers: Ogg pages demux ~400 LOC (Vorbis+Opus+FLAC); MP4 box walk ~600 LOC (AAC/ALAC); MKV EBML later.

## Video (deferred program)

MJPEG (JPEG frames in AVI) = first playable video; MPEG-1/2 via pl_mpeg (~4k, patents expired); H.264+/VP9/AV1/HEVC extreme + patent pools — skip. Demux needed: AVI ~500 LOC, MP4 ~700, MKV ~800, TS 188B.

## Architecture (recommended)

`kernel/codecapps/` (user-chosen): registry of `CodecDescriptor{name,kind,id,flags,probe,decode,encode}` ~32 slots; `codec_probe(buf,len)` by magic; decoders never allocate — caller-owned `FrameBuf`/`PcmBuf` + `Arena` over `pmm_allocate_pages`/`immt::memory_allocate`; reuse `videoframe.h`/`audioapi.h` types; generalize `videomcodec` (8-slot cap).

Port notes: stb/dr_libs/minimp3 honor `*_MALLOC`/`STBI_NO_STDIO` overrides → map to immt alloc; check `context.asm` SSE before enabling SIMD paths; cap decode sizes (fuzz history in stb).

Implementation order: (1) magic probe + BMP/PNM/TGA/QOI; (2) PNG inflate + stored-enc; (3) JPEG dec; (4) dr_flac + minimp3; (5) AVI+MJPEG (video program). Defer: Ogg/MP4 demux, Vorbis/Opus, WebP+, H.264+.

## Sources

stb (PD, `github.com/nothings/stb`), dr_libs (`github.com/mackron/dr_libs`), minimp3 (CC0, `github.com/lieff/minimp3`), qoi (MIT, `github.com/phoboslab/qoi`), pl_mpeg (MIT), alac (Apache-2.0, `github.com/macosforge/alac`), libopus (BSD). Specs: w3.org/TR/PNG, itu-t81.pdf, xiph.org/flac/format.html, RFC 6716/3533. Patents: MP3 expired 2017; AAC-LC ~2017, HE-AACv2 ~2030; H.264/HEVC Via LA pools.
