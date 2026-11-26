# HypeAI OS display roadmap

## Stage 1 — monitor information output (implemented)

- Read EDID through VBE/DDC `INT 10h AX=4F15h` before entering long mode.
- Copy up to four 128-byte EDID blocks into low memory.
- Validate the base-block checksum.
- Parse monitor name, manufacturer, product code, serial number, manufacture date, physical size, gamma and EDID version.
- Capture VBE/DDC and VBE/PM DPMS capability bits.
- Print the result through serial and framebuffer text.

Verified in QEMU: `QEMU Monitor`, manufacturer `RHT`, EDID 1.4, 26x19 cm, gamma 2.20, DDC caps `0x03`, DPMS states `0x0F`.

## Stage 2 — display scanout control (partially implemented)

Already implemented:

- VBE linear framebuffer mode selection;
- pixel, rectangle, gradient and text drawing;
- 24/32-bpp direct-color framebuffer support.

Next:

- dynamic VBE mode enumeration instead of fixed `0x118`;
- mode selection by width/height/bpp/refresh preference;
- VGA-compatible blank/unblank scanout control where hardware supports it;
- double buffering and vsync-aware page flipping after memory management exists.

## Stage 3 — monitor device control

Required for real brightness, contrast, input source and monitor power:

1. PCI/PCIe enumeration.
2. GPU-specific I2C/DDC channel driver.
3. EDID re-read through the native channel.
4. MCCS capability string parsing.
5. DDC/CI VCP commands:
   - `0x10` brightness;
   - `0x12` contrast;
   - `0x60` input source;
   - `0xD6` power mode;
   - `0x0C` save settings.
6. HypeAI display service API over IPC.

VBE/PM can report DPMS capability at boot, but runtime BIOS calls are unavailable in long mode. Therefore runtime power control needs VM86 or native drivers; native drivers are the correct long-term design.

## Stage 4 — HDR detection

- Parse CTA-861 extension blocks.
- Detect HDR Static Metadata Data Block, extended tag `0x06`.
- Parse supported EOTF bits: traditional gamma, PQ and HLG.
- Parse desired max luminance, frame-average luminance and min luminance when present.
- Parse colorimetry blocks for BT.2020 support.

The parser scaffolding is present in `hcpp/display/edid.hpp`; QEMU's current EDID does not advertise HDR static metadata.

## Stage 5 — HDR colorspace support

Adapted from XER's color-space model:

- `sRGB`;
- `DisplayP3`;
- `Rec2020`;
- `Rec2100_PQ`;
- `Rec2100_HLG`;
- `ScRGB`.

This model is implemented in `hcpp/display/color_space.hpp`.

Important hardware boundary: legacy VBE does not expose an API to enable HDR signaling or send HDMI/DisplayPort HDR metadata. Actual HDR activation requires:

1. native GPU modesetting;
2. 10-bit or higher scanout format;
3. HDMI AVI/DRM infoframes or DisplayPort SDP metadata;
4. correct transfer function signaling (`PQ` or `HLG`);
5. tone mapping according to EDID static metadata.

## Stage 6 — user-facing control service

Planned HypeAI service API:

```text
display.list()
display.info(id)
display.set_mode(id, width, height, refresh, bpp)
display.set_brightness(id, percent)
display.set_input(id, source)
display.set_power(id, on|standby|suspend|off)
display.hdr.enable(id, Rec2100_PQ | Rec2100_HLG)
display.hdr.disable(id)
```

This service should live outside the microkernel and communicate through HypeIPC after userspace exists.
