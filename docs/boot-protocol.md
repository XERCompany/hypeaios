# HypeAI OS boot protocol

Target: x86_64 legacy BIOS, no Linux or existing bootloader in the target path.

## Disk layout

- LBA 0: HypeBoot Stage 1, exactly one 512-byte boot sector.
- LBA 1-32: HypeBoot Stage 2, fixed 32-sector region.
- LBA 33-96: HypeCore kernel image, fixed 64-sector region.
- Later sectors: reserved for boot modules and a future filesystem.

## Boot chain

1. BIOS loads Stage 1 to physical address `0x7C00` and jumps to it in 16-bit real mode.
2. Stage 1 uses BIOS INT 13h extensions to load Stage 2 to `0x7E00`.
3. Stage 2 loads the kernel to physical address `0x10000`.
4. Stage 2 enables A20, installs a temporary GDT, builds identity-mapped page tables, enters protected mode, enables PAE and long mode, then jumps to the kernel entry point.
5. The kernel starts in 64-bit long mode with interrupts disabled and the first 2 MiB identity-mapped through a 2 MiB page.

## Current limitations

- INT 13h extensions are required; CHS fallback is not implemented yet.
- The kernel is loaded at a fixed low physical address and is not relocatable.
- E820 memory map capture, boot information structure, framebuffer handoff, and ELF loading are planned for the next milestones.
