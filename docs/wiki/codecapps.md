# Program: HypeAI CodecApps

Spec: GitHub issue [#23](https://github.com/XERCompany/hypeaios/issues/23), closed 2026-11-26. All tickets #24–#31 merged into `devin/hypeai-font-ui`. Research report: `docs/research/codecapps.md`.

## What exists (as of commit `345add0`)

Module `kernel/codecapps/` (note: research suggested `kernel/codec/`; spec/user chose `codecapps`).

- **Core** (#24): `CodecId` (27 entries), `CodecDescriptor` registry (32 slots), `codec_probe` magic sniffing (26 probes), `Arena` bump allocator (PMM/immt-backed), `codec report=` + per-codec `dec=yes|stub` lines + `codec deferred=` for probe-only formats.
- **Image** (`kernel/codecapps/image/`): BMP (8/24/32, top-down), PNM (P2-P6), TGA (raw+RLE, v2 footer), QOI (full), PNG (chunk walk + own `inflate` + stored-deflate encode), JPEG baseline (fixed-point IDCT, host-verified vs libjpeg), GIF (LZW+deinterlace), ICO (BMP & PNG-in-ICO delegation), EXIF (IFD0→ExifInfo), TIFF (raw/PackBits/LZW). Shared `imageutil.h` IO + `frame_prepare`→RGB32.
- **Audio** (`kernel/codecapps/audio/`): IMA-ADPCM + G.711 µ/A-law, MP3 via vendored minimp3 (CC0, `MINIMP3_NO_SIMD`), FLAC via vendored dr_flac (arena allocs), Ogg page walker with CRC + packet reassembly, Vorbis via vendored stb_vorbis (no-malloc over Arena), ALAC full path (cookie→elements→adaptive Rice→LPC→mid-side).
- **Integration** (#31): `codec integrated=ok`; embedded 40×40 PNG decoded through real codec path and blitted into demo desktop (`ui codec image=painted 40x40`); 32KB static arena for the blit (16KB was too small for PNG internals).
- **Test tooling**: `tools/gen_codec_clips.py` synthesizes all audio vectors (needs `pip install lameenc`; vendored fallback clips exist), `tools/mkjpeg_selftest.py` + `tools/jpeg_host_test.cc` for JPEG, `tools/vorbisgen.c`.

## Kernel-space consequences

- `kernel/runtime/builtins.cc` now also exports `abs/ldexp/floor/exp/log/pow/sin/cos/qsort` for stb_vorbis; `third_party/sqlite/freestanding/math.h` gained `ldexp`.
- kernel.bin ~992KB of a 2MB budget (KERNEL_SECTORS=4096).

## Deferred (honest stubs, `codec deferred=` in report)

PSD, WebP, JPEG2000, AVIF, HEIC, YUV-raw, WAV-mux (parser exists in kernel/audio), Opus, AAC, MP4. Video codecs are a separate future program (user decision 2026-11-26).
