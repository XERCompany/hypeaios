# Monitor control research

## What is controllable

There are two different layers that are often both called “monitor control”:

1. **Scanout control** — choosing the video mode and writing pixels. This is controlled by the GPU/display controller, not by the monitor itself.
2. **Monitor device control** — brightness, contrast, input source and power state. This is controlled inside the monitor firmware, normally through VESA DDC/CI / MCCS over the display cable's I2C channel.

## Legacy BIOS scanout

Under legacy BIOS, VESA VBE is the standard interface for selecting graphics modes while the CPU is still in real mode. Relevant interrupts:

- `INT 10h AX=4F00h`: query VBE controller information.
- `INT 10h AX=4F01h`: query one mode and fill a 256-byte mode information block.
- `INT 10h AX=4F02h`: select a video mode; bit 14 of `BX` requests a linear framebuffer.
- `INT 10h AX=4F10h`: VESA DPMS/power-related interface where supported.

Important mode-info fields:

- offset 16: bytes per scanline (`pitch`);
- offset 18: width in pixels;
- offset 20: height in pixels;
- offset 25: bits per pixel;
- offsets 31-38: color channel masks/positions;
- offset 40: physical address of the linear framebuffer.

VBE calls cannot be made after entering x86_64 long mode. The correct BIOS design is: query/select the mode in the bootloader, save the framebuffer information, map the framebuffer memory in page tables, then pass a boot-info structure to the kernel.

Sources:
- https://wiki.osdev.org/VESA_Video_Modes
- https://wiki.osdev.org/Drawing_In_a_Linear_Framebuffer

## Framebuffer programming

The pixel address calculation is:

```text
pixel = framebuffer + y * pitch + x * bytes_per_pixel
```

`pitch` must not be assumed to equal `width * bytes_per_pixel`; firmware may use padding. Pixel format must be read from the VBE color-mask fields instead of assuming a fixed layout.

## Monitor settings and DDC/CI

For real monitor settings, the industry interface is DDC/CI with MCCS Virtual Control Panel (VCP) codes. Commonly used examples:

- `0x10`: brightness;
- `0x12`: contrast;
- `0x60`: input source;
- `0xD6`: display power mode;
- `0x0C`: save current settings.

This requires an I2C/DDC channel exposed by the GPU. In HypeAI OS it cannot be implemented by VBE alone: it needs PCI enumeration, GPU-specific I2C access, EDID parsing, MCCS capability negotiation and a display-control service.

Sources:
- https://www.ddcutil.com/
- https://glenwing.github.io/docs/VESA-DDCCI-1.1.pdf

## Practical HypeAI OS plan

1. Bootloader: select VBE linear framebuffer mode.
2. Kernel: use framebuffer memory directly for pixels, rectangles, bitmaps and text.
3. HAL: model color format correctly from VBE fields.
4. Later display service: EDID + DDC/CI for monitor brightness/input/power.
5. Native GPU drivers: modesetting and multi-monitor support beyond firmware-selected scanout.

“Full monitor control” is therefore a long-term driver stack. The first implementable milestone is complete control of the visible pixel buffer; true monitor-internal controls require the DDC/CI layer.
