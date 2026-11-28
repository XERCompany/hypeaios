# Core OS P0 batch + HypeAIGDXF (P14)

Spec: `docs/research/core-os-improve.md` (research `575a53e`, merged `c87b8d6`, 2026-11-27). Tickets landed 2026-11-27 on `devin/hypeai-font-ui` under branches `ticket-p0a..p0e`, `ticket-flick`, `ticket-darkui`, `ticket-gdxf`. Row numbers below refer to the spec's §10 priority table.

## What landed (2026-11-27)

- **klog + panic** (P0-1/P0-2, `7aed39b`, merge `9fc70a8`): `kernel/klog.h` leveled/tagged logging over serial; `kernel/panic.{h,cc}` BSOD path — `idt.cc` exceptions now dump vector/regs/RIP/CR2 + error decode to serial *and* framebuffer, then cli+hlt. Ends the fault→return→re-fault storm the spec flagged as highest value-per-hour.
- **PCI scan + config helpers** (P0-3/P0-4, `35dae6c`, merge `8343d5f`): `pci.cc` bridge-recursive bus walk (functions 1-7 skipped when header-type bit7 clear), BAR/IRQ cache in `PciDevice`; `pci_io` gains `pci_bar_size` all-1s probe + Command-register/bus-master helpers. Unblocked every later DMA driver.
- **ATA write + hypeblob persistence** (P0-5/P0-6, `7e8be4f`, merge `ad8a21f`): `ata.cc` gains IDENTIFY/PIO write/FLUSH CACHE; `hypeblob_sync()` re-serializes dirty memfs entries back to the HYPEDATA region (fixed LBA 4129) — first on-disk persistence. Markers `ata model=`, `ata write=`, `blob ata=`.
- **RTC + CPUID** (P0-7, `4b978e0`, merge `341b828`): `kernel/time/rtc.cc` ports 0x70/0x71 BCD wall clock (`rtc=`); `kernel/arch/x86/cpuid.cc` feature struct (`cpu vendor=`); session layer shows wall clock.
- **PS/2 mouse IRQ12 + wheel** (P0-8, `21a9917`, merge `2838b32`): `input/msaux.cc` moved off the main-loop poll onto IRQ12; Intellimouse init (0xF3 200/100/80) enables 4-byte packets with wheel decode; events ride the existing `functionemitkeys` bus.
- **Damage compositor** (P1-5, `40781e2`, merge `da3ba5d`): `winman` keeps a dirty-rect list (`wm_invalidate`, `wm dirty=`); `input/cursprite.cc` cursor save/restore composites the sprite instead of repainting the whole desktop per mouse move — stops the widget flicker at ~100 Hz.
- **Dark theme** (`95b545c`, merge `add4167`): luminosity-tiered surfaces + soft shadows via `gfx/hypegfx` for hxrt apps; `theme dark=ok`.
- **HypeAIGDXF MVP** (P1-9, `3169d67`, merge `036acc7`): `kernel/gdxf/` — engine-owned loop with fixed timestep (`gdxf_run`/`begin_frame`/`end_frame`), back-buffer `Surface` + `gdxf_present` (blt model today, flip seam later), key/mouse state-snapshot input, hasio audio wrap, batched sprite + tilemap blits. Markers `gdxf init=ok surf=`, `gdxf run=ok`, `gdxf selftest=`; demo app `hxrt/apps/gdxfdemo.cc` (`gdxf demo win=`).

## Spec rows still open (as of 2026-11-28)

P1-7 fs_rename / wait-queue sleepers (fd table itself landed as #103 in P16), P1-8 preemptive scheduler, P1-10 watchdog; most P2 rows landed in P16/P17 — see [core-os-p16-p17.md](core-os-p16-p17.md). Deferred per spec: PCIe hot-plug/AER, journaling, UEFI BootInfo v3.
