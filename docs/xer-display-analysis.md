# XER display/HDR code analysis

Goal: reuse useful XER `src/*` design and code fragments in HypeAI OS without copying host-OS dependencies.

## Relevant XER components

### `src/ui/colorspace/color_space.h`

Useful abstractions:

- `ColorSpace`: `sRGB`, `DisplayP3`, `Rec2020`, `Rec2100_PQ`, `Rec2100_HLG`, `ScRGB`;
- `TransferFunction`: `sRGB`, `PQ`, `HLG`, `Linear`, `Gamma22`, `Gamma18`;
- helpers for names, HDR detection and default transfer functions.

These are portable concepts and are adapted into `hcpp/display/color_space.hpp`.

### `src/ui/colorspace/hdr_manager.h/.cc`

Useful abstractions:

- HDR metadata: maximum luminance, minimum luminance, frame-average luminance;
- capability/enabled state separation;
- preferred color space selection.

Not directly portable: the implementation is Windows/DXGI-oriented and currently mostly placeholder logic. HypeAI OS cannot use `HWND`, DXGI or Windows display APIs.

### `src/ui/colorspace/color_manager.h/.cc`

Useful design:

- system color space versus preferred color space;
- conversion through a linear intermediate;
- HDR manager ownership.

Not portable as-is: uses `std::vector`, `std::copy`, exceptions-capable hosted C++ and libm functions. Early HypeCore has no heap or libm.

### `src/content/platform/windows/winapi/graphics/winapi_graphics.cc`

Useful behavior:

- enumerate displays;
- report name, position, width, height and primary flag.

Not portable: depends on `EnumDisplayDevicesA`, `EnumDisplaySettingsA` and WinAPI. HypeAI OS must replace this with EDID, GPU scanout and future native display drivers.

### `src/image/core/color_space.h`

Useful color-space list and conversion direction. Current implementation targets packed RGBA8 image processing rather than display scanout negotiation.

## HypeAI OS adaptation plan

1. **Monitor information**: use VBE/DDC `INT 10h AX=4F15h` in HypeBoot to read EDID blocks before long mode. Parse monitor manufacturer, product, serial, manufacture date, size, gamma, feature bits, monitor name and extension count in `hcpp`.
2. **Monitor control**: short-term expose scanout/framebuffer control; true brightness/input/power requires DDC/CI MCCS over GPU I2C. VBE/PM DPMS capabilities can be captured at boot, but cannot be called after long mode without a VM86 monitor or native driver.
3. **HDR metadata**: parse CTA-861 extension blocks from EDID, especially HDR Static Metadata and colorimetry blocks.
4. **HDR colorspace**: adapt XER's `Rec2100_PQ`, `Rec2100_HLG` and `ScRGB` model into `hcpp`. Actual HDR scanout enablement requires native GPU programming and HDMI/DisplayPort metadata/infoframes; VBE does not provide an HDR enable API.
5. **Color conversion**: initially implement type-safe color-space metadata and simple integer/framebuffer operations; exact PQ/HLG conversion should be added after kernel floating-point or fixed-point math support is designed.

## Bottom line

XER contributes the display/color-space API shape. HypeAI OS must implement the hardware path independently because XER's monitor enumeration and HDR activation are host-Windows wrappers, while HypeAI OS runs below any host operating system.
