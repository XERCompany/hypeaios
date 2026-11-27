# HypeAI OS — Core roadmap

Drafted 2026-11-27. Sources: user direction + `docs/research/*`. Historical decisions in `docs/wiki/`.

## Standing rules (updated 2026-11-27)

- **No third-party libraries in new work.** Own `HypeAI.*` implementations only.
  Supersedes the earlier "port PD/MIT single-headers" decision; vendored code already merged
  (sqlite3, minimp3, dr_flac, stb_vorbis) stays for now — each is a *replacement candidate*
  in program P2, not a model for new code.
- Freestanding, `hcpp` vocab, noexcept, static storage, no comments, serial-marker acceptance.
- Drivers target what QEMU actually emulates first (intel-hda, ac97, bochs-display, std VGA,
  virtio-gpu, ahci, nvme, ehci), real hardware paths second.

## Program P1 — Display engine (user-specified file set)

Goal: real mode-setting + multi-display + scaling + capture-to-file, laid out as
`kernel/display/` modules. File names are spec-mandated (XER style); meanings assigned:

| File | Module |
|---|---|
| `displayport.h` | DP link layer: AUX channel transactions, lane/link training states, MSA packing |
| `displaydpi.h` | logical DPI → pixel scale table, per-output scale factors |
| `displaySize.h` | canonical mode list (WxH×Hz), aspect classes |
| `display-qxga.h` | QXGA 2048×1536 timings |
| `display-wvga.h` | WVGA 800×480 timings |
| `display-huxga.h` | HUXGA 6400×4800 timings |
| `clientdisplay.h` | client API surface: open output, get modes, set mode, present |
| `multidisplay.h` | multi-output topology registry |
| `multidisplay-mode.h` | span/mirror/extend policies |
| `multidisplay-swich.h` | active-output switch + re-blit path |
| `scaler.h` | integer scaler: nearest/bilinear (+bicubic tier) RGB32→format |
| `scaleuhd.h` | upscale path to UHD targets with edge rules |
| `writeframe.h` | present/blit pipeline: FrameBuf→scanout w/ vsync-ish pacing |
| `idscreen.h` | output identification: EDID serial→stable output id |
| `id-stat.h` | per-output status block (mode, link, last-present stats) |
| `foepnscr.h` | frame-open/screen-open session object for writers |
| `urecordscr.h` | screen-record: capture scanout→encode→file (uses codec path) |
| `lowbitrate.h` | bitrate tier table (low) for record/stream |
| `highbitrate.h` | bitrate tier table (high) |
| `displayhda.h` | HDMI/DP audio-over-display bridging to HDA |
| `pal.h` | PAL 625-line timings (legacy/analog out) |
| `uhdtv2.h` | UHDTV mode tables (4K/8K CTA-861 classes) |
| `uhdtv2-vision.h` | colorimetry/HDR metadata for UHD paths |
| `hgc.h` | HypeAI Graphics Controller facade — the public entry point other modules call |

Proposed additional files (same naming style): `dpxaux.h` (AUX primitive split out of displayport), `msthub.h` (DP MST topology), `scrnmap.h` (screen→fb mapping), `edidcta.h` (CTA-861 blocks beyond base EDID), `fmtcvt.h` (pixel-format convert inside writeframe).

Tickets (draft): D1 mode tables+scaler+writeframe → D2 EDID/output-id+clientdisplay → D3 DP/AUX + training state machine → D4 multidisplay → D5 urecordscr+bitrate tiers → D6 UHDTV/PAL/HDR metadata.

## Program P2 — HypeAI Codecs v2 (all formats, own code)

- Coverage matrix lives in `docs/research/os-core-roadmap.md` §5 (research in flight).
- Replace vendored decoders with own `HypeAI.*` implementations incrementally (MP3 first — well-understood), keep ABI through `CodecDescriptor` so `codec report` honesty stays.
- New own implementations first where specs are small: DDS, ICO-complete, BMP/TGA done ✓, TGA-16/XBM, ADPCM variants, MOD/XM/S3M tracker formats (tiny parsers, big win), MIDI-SMF, APNG.
- Video decode program (deferred earlier) folds in here: MJPEG → MPEG-1/2 → Theora → H.264-baseline as the ladder.
- Acceleration reality: no OpenCL-capable device in QEMU; real path = integer/fixed-point kernels + SSE2/AVX2 intrinsics in freestanding + kernel threads. Direct-PCIe compute offload = far future, design `CodecDescriptor` to allow device-backed impls later.

## Program P3 — Storage & persistence

AHCI (QEMU `-device ich9-ahci`) + NVMe-lite; block layer; filesystem write path; sqlite VFS upgrade memdb→block-backed → **persistent credentials/world state across reboots**. Unlocks real enroll/lockout persistence for Hello.

## Program P4 — Audio pro

ASIO-shaped public API (low-latency exclusive stream, kernel-bypass mixing) implemented our own way — Steinberg SDK is licensed, so the *interface contract* (period sizes, callback, <10ms roundtrip) is what we port, not the SDK. Kernel mixer + integer SRC (linear→windowed-sinc tier), tracker/MIDI playback via P2 decoders.

## Program P5 — Video capture

`videocapture.cc` skeleton → virtual capture device → UVC USB webcam path once xHCI exists (depends P3-adjacent USB work); V4L2-shaped API for consistency.

## Program P6 — Platform hardening

SMP/AP startup (MP spec), CPUID topology, perf counters, CPU/MEM accounting, thread model review, eventual ring-3 boundary.

## Program P7 — UI design language v3

From user references (2026-11-27): soft drop shadows (low opacity, negative spread), layered inner shadows for depth, **dark mode = elevation via surface luminosity not shadow**, cosmic/dark visual system. Lands on top of UI v2 primitives (alpha + rrect already exist).

## P8 — Boot & Type (splash + font engine + pretty UI)

User request (2026-01-16): Windows-style boot sequence; text currently pixel-ugly — build own HypeAIFont / HypeAITextRender / HypeAIEngineGraphics / HypeAIRenderUI; reference typeface Montserrat (SIL OFL — font data is an asset, rasterizer is ours).

| # | Ticket | Depends |
|---|--------|---------|
| T1 | `tools/genfont.py` — Montserrat TTF → SDF atlas (EDT) + metrics/kerning → embedded `.hfnt`; `kernel/font/` HypeAIFont loader | — |
| T2 | HypeAITextRender — SDF glyph blit (crisp any size), wrap/align/measure, UTF-8 incl. Cyrillic | T1 |
| T3 | HypeAIEngineGraphics — soft shadows (box-blur alpha), gradients, inner shadow, glow, elevation surfaces | — |
| T4 | HypeAIRenderUI — restyle widgets on T2+T3 (rounded, soft-shadow, Montserrat everywhere) | T2, T3 |
| T5 | `bootsplash` — logo + spinner + progress bar, driven from main.cc init stages | — |
| T6 | Polish pass + screenshot verification | T4, T5 |

## P9 — Shell & Runtime (user program layer)

User direction (2026-11-27): remove demo entirely; registration/login session; `.hxer` executables (`.hxersys` = system apps, non-deletable); services load in background while login shows; RevServices service manager + RevDBGService.hxersys control panel (restart/stop/logs); DXBackgroundDesktop.hxer wallpaper app; SpeedStart.hxersys launcher; proprietary path scheme `HYPEOS://<DiskChar>/`; .hxer windows get close button + menu.

| # | Ticket | Depends |
|---|--------|---------|
| S1 | `hxrt` runtime — .hxer descriptor {magic,name,ver,entry,flags SYS/USER}, app registry, exec into window | — |
| S2 | `winman` — window chrome (title/close/menu), focus, z-order, app window slots | S1 |
| S3 | `hypepath` — `HYPEOS://<char>/` resolver + memfs VFS (S=system non-del, U=user, T=temp) | — |
| S4 | `RevServices` — service registry {name,state,start_fn,log_ring,autostart}, restart/stop API, boot autostart | — |
| S5 | `RevDBGService.hxersys` — service list UI → control panel (restart/stop/log view) | S2,S4 |
| S6 | `DXBackgroundDesktop.hxer` — wallpaper: decode via codec path → desktop bg | S2 |
| S7 | `SpeedStart.hxersys` — launcher grid of registered .hxer apps | S2,S3 |
| S8 | Session — registration flow + login gate; services start in background behind greeter; demo.cc removed | S2,S4 |
| S9 | `hypefile` — .hxer/.hxersys file entries in memfs at HYPEOS://S/apps/, delete guard on .hxersys | S3,S1 |

