# HypeAI OS boot protocol

Target: x86_64 legacy BIOS, no Linux or existing bootloader in the target path.

## Disk layout

- LBA 0: HypeBoot Stage 1, exactly one 512-byte boot sector.
- LBA 1-32: HypeBoot Stage 2, fixed 32-sector region.
- LBA 33-288: HypeCore kernel image, fixed 256-sector region.
- Later sectors: reserved for boot modules and a future filesystem.

## Boot chain

1. BIOS loads Stage 1 to physical address `0x7C00` and jumps to it in 16-bit real mode.
2. Stage 1 uses BIOS INT 13h extensions to load Stage 2 to `0x7E00`.
3. Stage 2 loads the kernel to physical address `0x10000`.
4. Stage 2 captures the BIOS E820 memory map at physical `0x5500`, queries VBE/DDC EDID data, captures VBE/PM capabilities, selects VBE mode `0x118` with a linear framebuffer, and stores boot data in the boot-info structure at physical `0x5000`.
5. Stage 2 enables A20, installs a temporary GDT, builds identity-mapped page tables for the first 4 GiB, enters protected mode, enables PAE and long mode, then jumps to the kernel entry point.
6. The kernel starts in 64-bit long mode with interrupts disabled and `RDI` pointing to the boot-info structure.

## Boot-info structure v2

Physical address: `0x5000`; size: 64 bytes; packing: packed/little-endian. EDID blocks are stored separately at physical `0x5200`, up to four 128-byte blocks. E820 entries are stored at physical `0x5500` as packed 20-byte records `{u64 base, u64 length, u32 type}`, up to 32 entries.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u32 | magic `0x48595045` |
| 4 | u32 | protocol version, currently `2` |
| 8 | u32 | physical linear framebuffer address |
| 12 | u32 | bytes per scanline (`pitch`) |
| 16 | u16 | width in pixels |
| 18 | u16 | height in pixels |
| 20 | u8 | bits per pixel |
| 21 | u8 | red channel bit position |
| 22 | u8 | green channel bit position |
| 23 | u8 | blue channel bit position |
| 24 | u8 | red channel bit count |
| 25 | u8 | green channel bit count |
| 26 | u8 | blue channel bit count |
| 27 | u8 | flags; bit 0 means framebuffer present |
| 28 | u16 | selected VBE mode |
| 30 | u16 | reserved |
| 32 | u32 | physical address of EDID block storage |
| 36 | u8 | number of EDID blocks successfully read |
| 37 | u8 | VBE/DDC capability bits |
| 38 | u8 | EDID extension count from block 0 |
| 39 | u8 | VBE/PM supported DPMS states |
| 40 | u8 | nonzero when VBE/PM is supported |
| 41 | u32 | physical address of E820 entry storage |
| 45 | u8 | number of E820 entries captured |
| 46 | u8 | reserved |
| 47 | u8 | BIOS boot drive number |
| 48 | u32 | physical kernel load address |
| 52 | u32 | kernel image size in bytes |
| 56-63 | bytes | reserved |

## Current limitations

- INT 13h extensions are required; CHS fallback is not implemented yet.
- VBE mode `0x118` is fixed at 1024x768; dynamic mode enumeration is a next milestone.
- Both 24-bpp and 32-bpp direct-color framebuffers are accepted by the kernel.
- The kernel is loaded at a fixed low physical address and is not relocatable.
- ELF loading is planned for a later milestone.
- E820 memory-map capture, EDID block reading and VBE/PM capability capture are implemented.
- Runtime DPMS calls are unavailable after long mode because BIOS services cannot be called directly; runtime monitor power control requires VM86 or native GPU/I2C drivers.
- Monitor-internal controls such as brightness and input source require a future EDID/DDC/CI display service; they are not available through the linear framebuffer.
