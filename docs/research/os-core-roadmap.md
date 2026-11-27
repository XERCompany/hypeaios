# HypeAI OS — Core Roadmap Research (spec index + reality checks)

Scope: freestanding x86-64 kernel, C++20, no STL, **no third-party code** — all targets are hand-written `HypeAI.*` implementations. Spec hub for everything below: <https://wiki.osdev.org/Technical_Specifications>

## 1. Kernel subsystems

| Subsystem | Spec / doc | Minimal viable scope |
|---|---|---|
| SMP / AP startup | Intel MP Spec 1.4 <https://files.serialport.org/Intel/download.intel.com/design/archives/processors/pro/docs/24201606.pdf>; Intel SDM Vol 3A §8 (APIC, INIT–SIPI–SIPI) <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html> | Parse MADT (ACPI), trampoline in low mem, INIT+2×SIPI per AP, per-CPU data + IPI TLB shootdown |
| ACPI / AML | ACPI 6.6, ch.20 AML grammar <https://uefi.org/specs/ACPI/6.6/20_AML_Specification.html>; ACPICA as behavioral reference <https://acpica.org/> | Tier-1: RSDP→RSDT→MADT/FADT parse only. Tier-2 (own AML interpreter): namespace, `Def*`, method eval, `_PIC/_PRT/_CRS` for IRQ routing. Skip GPE/S-states/battery |
| AHCI SATA | AHCI 1.3.1 <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf> | One port: FIS register H2D/D2H, PRDT DMA, IDENTIFY + READ/WRITE (28/48-bit). Skip NCQ, PM, hotplug |
| NVMe | NVMe Base spec <https://nvmexpress.org/specification/nvm-express-base-specification/>; QEMU device <https://www.qemu.org/docs/master/system/devices/nvme.html> | Admin + 1 I/O queue pair, Identify, Read/Write — simplest storage driver that exists |
| USB | xHCI 1.2 <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf>; EHCI 1.0 <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/ehci-specification-for-usb.pdf>; UHCI 1.1 design guide <https://mirrors.osuosl.org/pub/netwinder/pub/misc/docs/29765002-usb-uhci%20design%20guide.pdf>; QEMU USB <https://www.qemu.org/docs/master/system/devices/usb.html> | xHCI only (QEMU recommends it; covers USB1/2/3): command/event rings, control transfers, enumeration, HID + bulk. Skip isoch initially |
| RTC / NVRAM | MC146818A datasheet <https://www.nxp.com/docs/en/data-sheet/MC146818A.pdf> | Ports 0x70/0x71, BCD/binary clock read, periodic IRQ8 (2–8192 Hz), alarm IRQ — trivial |
| CPUID topology | Intel SDM Vol 2A CPUID (leaves 1, 4, 0xB/0x1F) <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html> | Enumerate cores/threads, feature bits (SSE/AVX), feeds SMP + SIMD init |

## 2. Display / video subsystem

- **DisplayPort / eDP**: official spec is VESA member-only (<https://vesa.org/featured-standards/>); full DP 1.2 text mirrored: <https://glenwing.github.io/docs/DP-1.2.pdf>. Layers to implement: AUX channel I2C-over-AUX (DPCD read), link training TPS1 (clock recovery) → TPS2 (channel EQ) per TI app note <https://www.ti.com/lit/an/sdaa295/sdaa295.pdf>, then Main Stream Attributes (MSA) packet + SST stream. eDP skips HPD/connector logic; panel power sequencing is board-specific. Real hardware only — QEMU has no DP.
- **EDID extensions** (parser already exists): next targets = CTA-861 extension block (CEA video timing/audio descriptors) <https://ia800407.us.archive.org/26/items/ansi-cta-861-h-final/ANSI-CTA-861-H-Final.pdf>; DisplayID 2.x replacement <https://en.wikipedia.org/wiki/DisplayID>; base EDID 1.4 ref <https://glenwing.github.io/docs/VESA-EEDID-A2.pdf>.
- **Resolution table**: canonical names QXGA 2048×1536, WVGA 800×480, HXGA 4096×3072, HUXGA 6400×4800 — <https://en.wikipedia.org/wiki/Computer_display_standard>; UHDTV 4K/8K 3840×2160/7680×4320 — ITU-R BT.2020 (free) <https://www.itu.int/rec/R-REC-BT.2020>.
- **QEMU GPU paths**: `-vga std` (Bochs VBE dispi ports 0x1CE/0x1CF) <https://wiki.osdev.org/Bochs_VBE_Extensions>; `bochs-display` (same regs + PCI BAR, multi-head capable) — QEMU `docs/specs/` <https://github.com/qemu/qemu/tree/master/docs/specs>; `virtio-gpu` 2D only = EDID + resource/flush commands, spec <https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html>, QEMU doc <https://www.qemu.org/docs/master/system/devices/virtio-gpu.html>. **VirGL = out of scope**: needs a guest OpenGL/Vulkan driver <https://docs.mesa3d.org/drivers/virgl.html>.
- **VBE vs GOP vs native**: VBE int10 only works in real mode/pre-OS (<http://www.petesqbsite.com/sections/tutorials/tuts/vbe3.pdf>); UEFI GOP gives a fixed framebuffer, no modeset after ExitBootServices (<https://uefi.org/specs/UEFI/latest>) → native drivers (bochs/virtio-gpu/real) are the only post-boot path.
- **SW scaling**: nearest (trivial) <https://en.wikipedia.org/wiki/Nearest-neighbor_interpolation>; bilinear — 4 taps, cheap in Q16.16 fixed point <https://en.wikipedia.org/wiki/Bilinear_interpolation>; bicubic — 16 taps, ~8× bilinear cost, still integer-feasible <https://en.wikipedia.org/wiki/Bicubic_interpolation>. Plan: NN for integer ratios, bilinear default, bicubic optional HQ path.
- **PAL (for pal.h)**: ITU-R BT.1700 (free) — 625 lines, 50 Hz field, 576 active lines, 15.625 kHz line rate <https://www.itu.int/rec/R-REC-BT.1700-0-200502-I/en>.
- **DP MST**: multi-stream transport (up to 63 streams over one link) is in the DP spec — defer; SST single-display first.

## 3. Audio pro ("ASIO-like")

- **ASIO itself**: Steinberg SDK now dual-licensed — proprietary EULA or GPLv3 (<https://www.steinberg.net/developers/asiosdk-open/>, <https://www.steinberg.net/developers/>). It's a Windows driver ABI; in your own kernel it buys nothing — implement the *concept* instead.
- **"ASIO-like" implementable in-kernel**: <10 ms round-trip via small periods (32–128 frames ≈ 0.7–2.7 ms @48 kHz) on the HDA DMA ring + position registers (Intel HDA spec <https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf>); JACK-style pull graph = mixer invoked per-period callback, zero-copy kernel bypass <https://jackaudio.org/api/>.
- **SRC (integer math)**: linear/ZOH = trivial but audibly poor; polyphase windowed-sinc in fixed point is the right target — theory ref <https://ccrma.stanford.edu/~jos/resample/>, quality reference points <https://libsndfile.github.io/libsamplerate/>. Single-kernel-mode mixer owns the only resample point → predictable latency.

## 4. Video capture

- **UVC**: full spec set <https://www.usb.org/document-library/video-class-v15-document-set> — VideoControl (probe/commit) + VideoStreaming (isoch bulk alt-settings), uncompressed payload doc sufficient for raw frames.
- **QEMU reality**: no UVC emulation in released QEMU — upstream patch series (9elements, 2025, `-device usb-video` + v4l2 backend) <https://lists.libreplanet.org/archive/html/qemu-devel/2025-07/msg02289.html>; `usb-host` passthrough explicitly does **not** support isochronous video <https://www.qemu.org/docs/master/system/devices/usb.html>. → UVC testing requires real hardware or waiting for that QEMU feature.
- **API shape**: model on V4L2 — `QUERYCAP`/`S_FMT`, buffer queue `REQBUFS`/`QBUF`/`DQBUF` <https://docs.kernel.org/userspace-api/media/v4l/v4l2.html>.
- **PCIe capture (BT878-class)**: datasheet <https://pvr.sourceforge.net/Bt878.pdf>; RISC-programmable DMA directly to memory, simple and well-understood (Linux bttv doc <https://docs.kernel.org/admin-guide/media/bttv.html>) — viable real-hw stretch goal, cards are cheap.
- **Realistic order**: virtual capture device first (generated colorbars/testsrc feeding the same queue API) → UVC on real hw → BT878 optional.

## 5. Codec catalog — "all formats"

Complexity: **t**rivial / **m**edium / **h**ard / **X** monster. Kernel-fit = write own `HypeAI.*` (Y), parse-only (P), or permanent stub (S).

**Image** (have: BMP PNG JPEG GIF TIFF QOI ICO-ish)

| Format | Cplx | Fit | Doc |
|---|---|---|---|
| ICO | t | Y | <https://learn.microsoft.com/en-us/windows/win32/menurc/icons> |
| DDS (BC1–3) | t/m | Y | <https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide> |
| APNG | t/m | Y | <https://wiki.mozilla.org/APNG_Specification> |
| PSD | m | P (flatten composite + RLE) | <https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/> |
| EXR/HDR | m | Y (scanline, ZIP/RLE first, PIZ later) | <https://openexr.com/en/latest/OpenEXRFileLayout.html> |
| WebP | m/h | Y (VP8L lossless first: RFC 9649 + VP8L spec; lossy VP8 later) | <https://www.rfc-editor.org/rfc/rfc9649>, <https://developers.google.com/speed/webp/docs/webp_lossless_bitstream_specification>, <https://www.rfc-editor.org/rfc/rfc6386> |
| JPEG 2000 | h | maybe | T.800 (08/02 free PDF) <https://www.itu.int/rec/T-REC-T.800-200208-S> |
| JPEG XL | h | maybe-late (spec paywalled; whitepaper free) | <https://jpeg.org/jpegxl/>, <https://ds.jpeg.org/whitepapers/jpeg-xl-whitepaper.pdf>, <https://www.iso.org/standard/85066.html> |
| AVIF | X | S (needs AV1 decoder) | <https://aomediacodec.github.io/av1-avif/v1.2.0.html> |
| HEIC | X | S (HEIF + HEVC) | H.265 free <https://www.itu.int/rec/T-REC-H.265> |

**Audio** (have: ADPCM G711 MP3 FLAC Vorbis ALAC)

| Format | Cplx | Fit | Doc |
|---|---|---|---|
| MIDI (SMF) | t/m | Y (parse trivial; render via own OPL-style FM or mini-wavetable synth) | <https://midi.org/standard-midi-files-specification> |
| MOD/XM/S3M/IT | t/m | Y — tiny parsers + mixer, best effort/fun ratio on the list | <https://wiki.openmpt.org/Manual:_Module_formats>, <https://milkytracker.org/docs/XM_file_format.pdf>, <https://formats.kaitai.io/s3m/> |
| AAC (ADTS/LATM) | h | Y (LC only; ADTS framing t, MDCT+PNS+TNS core h) | <https://webstore.ansi.org/preview-pages/INCITS/preview_INCITS+ISO+IEC+13818-7+2006+(R2019).pdf>, LATM <https://www.rfc-editor.org/rfc/rfc3016> |
| Opus | h | Y (CELT-only mode still h; SILK adds more) | <https://www.rfc-editor.org/rfc/rfc6716> |
| AMR(-NB/WB) | h | maybe (ACELP) | <https://www.etsi.org/deliver/etsi_ts/126000_126099/126090/19.00.00_60/ts_126090v190000p.pdf>, file fmt <https://www.rfc-editor.org/info/rfc4867/> |
| WMA | X | S — ASF container is public, WMA bitstream spec is not | <https://learn.microsoft.com/en-us/windows/win32/wmformat/overview-of-the-asf-format> |

**Video**

| Format | Cplx | Fit | Doc |
|---|---|---|---|
| MJPEG | t/m | Y (per-frame T.81 JPEG in AVI/MOV) | T.81 mirror <https://www.w3.org/Graphics/JPEG/itu-t81.pdf> |
| MPEG-1 video | h | Y (builds on existing JPEG DCT/Huffman skills) | <https://www.iso.org/standard/22411.html> |
| MPEG-2 video | h | maybe (spec paywalled) | <https://www.itu.int/rec/T-REC-H.262/en> |
| MPEG-4 ASP (DivX) | h | maybe | <https://www.iso.org/standard/39259.html> |
| Theora | h | maybe (VP3-derived, spec free) | <https://www.theora.org/doc/Theora.pdf> |
| VP8 | h | maybe | <https://www.rfc-editor.org/rfc/rfc6386> |
| H.264 | X | S (baseline-only still X: CAVLC+¼-pel MC+deblock; spec is free though) | <https://www.itu.int/rec/T-REC-H.264-202408-I> |
| VP9 | X | S | <https://www.webmproject.org/vp9/> |
| Anim GIF | t | ✓ done — GIF89a extension already covered |

**Containers**

| Container | Cplx | Fit | Doc |
|---|---|---|---|
| ADTS | t | Y | header syntax in 13818-7 §8 (see AAC row) |
| AVI (RIFF) | t/m | Y | <https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference> |
| MOV/MP4 (ISO BMFF) | m | Y | <https://www.iso.org/standard/85596.html> (+ box registry <https://mp4ra.org/>) |
| MKV/WebM (EBML) | m | Y | <https://www.rfc-editor.org/rfc/rfc8794> |
| ASF | m | P | <https://learn.microsoft.com/en-us/windows/win32/wmformat/overview-of-the-asf-format> |
| Ogg | — | ✓ done | — |

## 6. PCIe / OpenCL reality check

- **"Direct PCIe"** for codec offload = PCI config-space enumeration + BAR MMIO (<https://wiki.osdev.org/PCI_Express>); the PCIe Base spec itself is PCI-SIG member-only <https://pcisig.com/specifications>. MMIO alone gives **nothing** for codecs — offload needs a compute-capable GPU, and QEMU exposes none (virtio-gpu/VirGL is GL only, no OpenCL device <https://www.qemu.org/docs/master/system/devices/virtio-gpu.html>). Real Intel/AMD/NV compute stacks are monster-scope.
- **Real accel path = SIMD + SMP**: `immintrin.h`/SSE2/AVX2 intrinsics are *compiler-provided* builtins (GCC x86 built-ins <https://gcc.gnu.org/onlinedocs/gcc/x86-Built-in-Functions.html>) — allowed under the no-third-party-libs rule. Kernel must enable SSE/AVX (CR0/CR4/XCR0, XSAVE, 16/32-byte alignment) <https://wiki.osdev.org/SSE> and gate on CPUID leaf 1/7.
- **OpenCL**: spec <https://registry.khronos.org/OpenCL/> — far-future only; requires a whole GPU driver + scheduler stack first.

## Suggested program ordering

1. **RTC + CPUID topology** (trivial; unblocks timing/topology infra) → **SMP/AP startup** (INIT-SIPI-SIPI) → **ACPI**: table parse now, own AML interpreter later for `_PIC/_PRT`.
2. **Storage**: AHCI, then NVMe (NVMe is simpler — do early).
3. **xHCI** (+HID class) — prerequisite for UVC and any USB input.
4. **Display**: EDID ext (CTA-861 + DisplayID) → bochs-display/virtio-gpu modeset → Q16.16 bilinear scaler → DP/eDP link training on real hw → MST last.
5. **Audio low-latency**: fixed-point windowed-sinc SRC + per-period kernel mixer ("ASIO-like"), target <10 ms.
6. **Capture**: virtual test-source device with V4L2-shaped queue API → UVC on real hw (no QEMU path yet) → BT878 stretch.
7. **Codec sprint A (trivial/medium, all write-own)**: ICO, DDS-BC1/3, APNG, MJPEG+AVI, ADTS, tracker MOD/XM/S3M/IT, SMF+FM synth, PSD, EXR, WebP-lossless, EBML + ISO-BMFF parsers.
8. **SIMD infra** (CPUID gate + XCR0/XSAVE) → retrofit JPEG/MP3/FLAC/Vorbis hot loops; threads via SMP.
9. **Codec sprint B (hard)**: AAC-LC, Opus, MPEG-1/2, VP8, then maybe Theora/JPEG2000/JXL.
10. **Permanent stubs** (documented "unsupported codec" path): H.264, HEVC/HEIC, AV1/AVIF, VP9, WMA. **OpenCL/GPU compute: far future.**
