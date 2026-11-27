# HypeAI OS — Core Improvement Audit + HypeAIGDXF Proposal

Deep audit of the kernel's core subsystems against what mature hobby/production
OSes actually do. Every finding is grounded in the current tree
(`current: X -> recommended: Y`) so each row can become an implementation ticket.
Brother document: `docs/research/os-core-roadmap.md` (spec index + codec catalog).
This document is about *core-OS correctness and breadth*, not codecs.

Conventions: paths are repo-relative; `~effort` assumes a single engineer who
knows the subsystem; P0 = cheap + high value, P1 = medium, P2 = large.

---

## 1. PCI / PCIe

### Current state

- `kernel/pci/pci.cc:5-6` uses the legacy mechanism-#1 ports `0xCF8/0xCFC` only.
  No ECAM, no MCFG lookup — the extended 4 KiB config space is unreachable.
- `kernel/pci/pci.cc:49-58` `scan()` brute-forces **256 buses x 32 slots x 8
  functions** (65536 probes) into a fixed `max_devices = 64` array. It does not
  short-circuit function 1..7 when function 0's header-type bit 7 is clear, and
  64 entries will silently truncate a fully populated bus (each function is a
  separate entry; QEMU with virtio devices + bridges can approach this).
- `kernel/pci/pci.h:7-18` `PciDevice` stores IDs and class only — no BAR cache,
  no interrupt line/pin, no capability snapshot.
- `kernel/pci/pci_io.cc` can read/write BARs but there is **no BAR sizing
  helper** (write-0xFFFFFFFF probe) and **no helper to set the PCI Command
  register** (bits 0 I/O, 1 Memory, 2 Bus-Master). Without bus-master enable, no
  future DMA device (AHCI, NVMe, virtio, e1000) can work.
- `kernel/pci/pci_capability.h:4` knows cap IDs (PM 0x01, MSI 0x05, PCIe 0x10,
  MSI-X 0x11); `find_capability` walks the list. MSI is fully programmable
  (`pci_msi.cc:19-28` address+data+enable). **MSI-X is detection-only**
  (`msix_present`) — no table/PBA programming, despite MSI-X being what
  virtio-pci and NVMe actually want.
- No PCIe extended capability walk (offset 0x100+, needs ECAM), no AER, no
  hot-plug (PCIe native hotplug or ACPI GPE-based), no `_OSC` negotiation.

### What real OSes do

- Linux reads legacy space via ports below offset 256 and uses **ECAM via MCFG**
  for everything else (`arch/x86/pci/mmconfig-shared.c`, `pci_parse_mcfg()`);
  MCFG entries carry segment group + bus range + 64-bit ECAM base — QEMU i440fx
  exposes base `0xB0000000` buses 0..255, Q35 `0xB0000000` too.
- BAR sizing is done once at enumerate time; BARs and the interrupt pin are
  cached in the device object so drivers never re-probe config space.
- MSI-X: cap Message Control bits [10:0] = table size-1 (up to 2048 vectors);
  Table Offset/BIR and PBA Offset/BIR pick a BAR + 8-byte-aligned offset; each
  16-byte table entry = {addr lo, addr hi, data, vector control bit0 mask};
  enable via Message Control bit 15, global mask via bit 14, per-vector mask in
  the entry, pending bits in the PBA (read-only).
- Enumeration uses recursive descent through PCI-to-PCI bridges (header type 1:
  secondary bus number at config offset 0x19) rather than a flat 256-bus sweep —
  faster and finds devices behind bridges.

### Recommended changes (this kernel)

1. `pci.cc`: add bridge-aware recursive enumeration starting at bus 0; skip
   functions 1-7 when `header_type & 0x80 == 0`; raise `max_devices` to 256.
2. `pci_io.cc`/`pci.h`: cache up to 6 BARs (decoded: mem/io, 64-bit flag, size,
   address) + interrupt line/pin in `PciDevice` at scan time; add
   `pci_bar_size(dev, idx)` (save, write all-1s, read, restore);
   `pci_command_set(dev, flags)` / `pci_enable_bus_master(dev)`.
3. `kernel/acpi/mcfg.{h,cc}` (new): `find_table("MCFG")` already exists
   (`acpi.cc:65`) — parse entries {base, segment, start bus, end bus}; add
   `pci::ecam_read*/write*` MMIO path used automatically when `bus/offset`
   exceeds legacy reach; keep CF8/CFC as fallback for offsets < 256.
4. `kernel/pci/pci_msix.{h,cc}` (new): `msix_table_map(dev)` (compute VA from
   BIR+offset), `msix_program_vector(dev, index, lapic_addr, data)`,
   `msix_set_mask`, `msix_enable/disable`. Required by virtio-pci modern and
   NVMe.
5. Interrupt routing: read interrupt line at scan; note that QEMU's PIIX3 IRQ
   steering already maps INTA-D; document that MSI bypasses PIC routing (this
   matters once IOAPIC is up — §2).
6. Defer: PCIe ext-cap walk (AER), hot-plug via ACPI GPE/`_OSC` — real-hardware
   concerns, QEMU has no practical hotplug test path for this project's targets.

Sources:
- <https://wiki.osdev.org/PCI> (BAR sizing, MSI/MSI-X capability layout)
- <https://wiki.osdev.org/PCI_Express> (ECAM, MCFG table format, segment groups)
- <https://kernel-internals.org/interrupts/msi-msix/> (MSI-X table/PBA layout)
- <https://github.com/intel/nemu/wiki/ACPI-PCI-discovery-hotplug> (real QEMU
  MCFG dump: `BOCHS BXPCMCFG`, base 0x80000000-style entries)
- <https://aravindsk.com/notes/pcie-boot.html> (Linux ECAM/port split)

---

## 2. Boot / platform bring-up

### Current state

- `boot/stage2.asm`: custom BIOS loader. Collects E820 (line 141-169, max 32
  entries, **20-byte records only** — no ACPI 3.0 extended-attributes dword),
  EDID via `int10 4F15`, DPMS via `4F10`, fixed VBE mode `0x118` (line 14,
  1024x768x32), A20 via port 0x92, kernel loaded by `int13 AH=42` EDD packets
  bounced through `int15 AH=87`. Long mode: PML4 at 0x70000, **2 MiB pages
  identity-mapping 4 GiB** (lines 406-424), flat GDT, jumps to
  `KERNEL_LOAD_ADDR=0x100000` with `rdi = BootInfo`.
- `kernel/boot_info.hpp`: 64-byte packed `BootInfo` v2 — fb geometry, EDID
  phys, E820 phys+count, boot drive, kernel phys/size. No RSDP field (ACPI is
  re-found by scanning EBDA/0xE0000, `acpi.cc:57-64` — fine for BIOS boot,
  blocks a future UEFI path where there is no EBDA), no module list, no
  cmdline.
- `kernel/acpi/acpi.cc`: RSDP scan + XSDT/RSDT walk; recognizes only `APIC`
  (MADT) and `FACP` (FADT). `find_table` works for any signature but nothing
  calls it for **MCFG, HPET, SSDT/DSDT**; `madt.cc` counts LAPICs/IOAPICs/ISOs
  but **no code programs the IOAPIC or LAPIC** — interrupts still run on the
  legacy PIC (`idt.cc:53-63` remaps 8259, `irq.cc` dispatches IRQ0-15).
- Timing: PIT only, fixed 100 Hz (`idt.cc:78-86`). No TSC calibration, no HPET,
  no APIC timer, no RTC read. `FrameStats.last_us` and any `*_us` fields
  therefore derive from ~10 ms-granularity ticks.
- CPU identification: `apic.cc` CPUID leaf 1 only (APIC/x2APIC bits). No feature
  detection (SSE/AVX/XSAVE/invariant-TSC), no vendor/family/model string, no
  topology (leaf 0xB/0x1F).
- `pmm.h:9`: PMM manages only the first **128 MiB** regardless of E820 —
  adequate today but the cap should derive from the map, not be hard-coded.
- `idt.cc:35-46`: exceptions print vector+error to serial and **return** — for
  faults (#PF, #GP) the CPU re-executes the faulting instruction and re-faults
  forever (ISR-storm). No register dump, no CR2 dump, no halt.

### What real OSes do

- Bootflow hygiene: Limine/Multiboot2-style boot info carries RSDP pointer,
  memory map (with reserved/ACPI-reclaimable/bad-RAM types, not just usable),
  kernel cmdline, and module list so early file loading doesn't depend on disk
  drivers.
- Memory map sanitization: sort, merge, clip the E820; treat type-2
  (reserved), 3 (ACPI reclaimable), 4 (NVS) distinctly; extend entries to the
  24-byte form to get the `ignore` attribute bit.
- Feature detection gate: a `cpu.cc` running CPUID leaves 0,1,7,0x80000001,
  0x15 (TSC Hz on newer Intel), 0xB/0x1F (topology); enables SSE (CR4.OSFXSR),
  sets CR0.WP, aligns on invariant TSC before trusting `rdtsc` for time.
- Timer ladder: PIT -> calibrate TSC (measure over a known PIT/HPET window,
  2-3 samples to reject SMM noise) -> enable LAPIC timer (calibrate against
  TSC or PIT; TSC-deadline mode if CPUID.1:ECX[24]) -> keep PIT off afterwards.
  HPET (ACPI `HPET` table, MMIO base at FED0xxxx on QEMU) is the best
  calibration reference: 64-bit counter at >=10 MHz.
- Interrupt routing: program the IOAPIC(s) found in MADT + ISO overrides
  (level/edge, polarity) so IRQs land on chosen vectors; LAPIC via the base
  address MADT reported (`madt.cc` already computes `effective_lapic_address`);
  enable via LAPIC SVR bit 8; send EOI to LAPIC not PIC.
- Panic: Linux `panic()`/Windows BSOD/SerenityOS "Kernel Panic" screen all do
  the same thing — stop the world, dump GPRs + CR2 + RIP/CS/RFLAGS + error
  decode (P/W/U/I/D, PK/SSGX...), print it to *every* output (serial AND
  framebuffer text), then cli+hlt. Never return from faults.

### Recommended changes

1. **`kernel/panic.{h,cc}` (new)**: exception path in `idt.cc` calls
   `panic_dump(frame)` -> serial + framebuffer BSOD (vector name, error-code
   decode, RIP/CS/RFLAGS/CR2, GPRs), then cli+hlt. Highest value-per-hour in
   this document.
2. **`kernel/arch/x86/cpuid.{h,cc}` (new)**: feature struct {vendor, family,
   model, sse/avx/xsave/invariant_tsc/tsc_deadline/max leaf}; call once at
   boot, gate APIC timer mode and SIMD enablement on it.
3. **`kernel/time/` (new dir)**: `rtc.cc` (ports 0x70/0x71 BCD clock — trivial,
   already in roadmap), `tsc.cc` (rdtsc + calibration vs PIT or HPET, expose
   `tsc_hz()`, `time_us()`), `apic_timer.cc` (LAPIC timer one-shot/periodic on
   a private vector; replaces IRQ0 as the scheduler tick).
4. **`kernel/acpi/ioapic.{h,cc}` + LAPIC MMIO init**: use `madt_info()` (already
   parsed: address, IOAPIC base, ISOs); route IRQ1/IRQ12 for PS/2 through the
   IOAPIC; keep PIC masked (APCI `pc_at_compatible` flag already parsed).
5. **`mcfg.cc`** as per §1; `hpet.cc` (ACPI table signature `HPET`, MMIO
   counter) as calibration reference + watchdog source.
6. `stage2.asm`: bump E820 records to 24 bytes (max 48 entries), add
   `rsdp_hint` field to `BootInfo` (v3) — cheap insurance for a future
   UEFI/multiboot2 path; not urgent while BIOS-only.
7. `pmm.cc`: derive managed limit from E820 instead of the 128 MiB constant.

Sources:
- <https://wiki.osdev.org/Timer_Interrupt_Sources> (PIT/HPET/TSC/LAPIC-timer
  comparison table)
- <https://baponkar.github.io/Osdev-Notes/02_Architecture/08_Timers.html>
  (LAPIC timer calibration walkthrough)
- <https://forum.osdev.org/viewtopic.php?t=57337> (TSC calibration practice:
  PIT window, invariant-TSC check, resampling)
- <https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html> (module
  + mmap tag model to copy for BootInfo v3)
- `docs/research/os-core-roadmap.md` §1 (MP Spec INIT-SIPI-SIPI, ACPI tier
  plan, RTC datasheet links)

---

## 3. UI render / compositing

### Current state

- `kernel/ui/canvas.h:11-46`: **immediate-mode** software canvas drawing
  directly into the VBE scanout framebuffer; clip = a single `Rect`.
- `kernel/win/winman.h:26-31`: `wm_paint(fb)` repaints the whole desktop; no
  damage list, no back buffer.
- `kernel/main.cc:792-800`: main loop polls cursor every PIT tick (~10 ms); on
  any cursor movement it calls `desk_paint(fb)` (full repaint of every window)
  then `cursor_draw(fb)` which composites the cursor sprite **straight into
  scanout** — every mouse wiggle repaints the world at ~100 Hz.
- `kernel/display/writeframe.{h,cc}`: scaled/format-converted blit with
  `FrameStats` — the only "present" primitive; no vsync concept, no flip.
- No dirty-rect tracking anywhere; `ui/geometry.h` has `Rect` but no rect list
  / region type. No texture/sprite batching; `gfx/*` are one-shot effects.
- VBE has no vsync/modeset in long mode → current architecture can't tear-free
  present until a real display driver (bochs-display/virtio-gpu, per
  `docs/display-roadmap.md`) exists.

### What real OSes do

- SerenityOS WindowServer: windows paint into their own bitmaps; compositor
  keeps a **dirty rect list**, clips each window's blit to dirty ∩ occlusion
  and flushes to the back buffer on a timer; a `flash_flush` debug mode
  highlights what actually repainted — huge for finding over-invalidation.
- Windows DWM/DXGI: three present tiers — *Composition* (app buffer composed
  into DWM back buffer, then flip), *DirectFlip* (app buffer scanned out
  directly, skipping the compose), *Independent Flip* (display engine flips
  with no compositor involvement). Flip model = swap chain of ≥2 buffers where
  the scanout base-pointer change takes effect at VSYNC.
- Haiku app_server: same model — per-window bitmaps, dirty regions,
  single-threaded compositing loop fed by port messages.
- Damage discipline: widgets mark only the rect they changed; moving a window
  dirties old-frame ∪ new-frame; text caret blink dirties ~40 px² not 2.4 Mp.

### Recommended changes

1. **Back buffer + damage** (no HW dependency): allocate a PMM back buffer;
   `Canvas` draws into it; keep `DamageList` (fixed cap, e.g. 32 rects, union
   on overflow -> full repaint). `wm_paint` becomes `wm_compose`: blit only
   damaged windows' damaged sub-rects to back buffer, then copy dirty rects to
   scanout (banded memcpy). Cursor: save-under or, simpler, composite cursor
   into back buffer during flush only — never into scanout directly.
2. `hype::ui::Region` (rect list, union/intersect/subtract) in
   `kernel/ui/geometry.h` or new `region.h` — needed by wm + GDXF present.
3. Widget `invalidate(Rect)` entry point (`widget.h` gets `mark_dirty`);
   `wm_handle_mouse`/`wm_focus` dirty old∪new frames instead of full repaint.
4. Present model: treat current `write_frame` as the "blt-model" path; add
   `display::present(src, dirty[], n)` that is a memcpy today but is the seam
   where bochs-display/virtio-gpu page-flips slot in later. Defer true flip
   chains until a modesetting driver exists (roadmap step 4).
5. Optional debug: "flash flush" repaint-highlight toggle (Serenity's
   `wsctl -f` pattern) — one bool in `deskman`, priceless for tuning damage.

Sources:
- SerenityOS WindowServer dirty-rect work, e.g. commit "Only blit dirty rect
  of windows to back buffer" and `WindowServer.ipc` `invalidate_rect`:
  <https://github.com/SerenityOS/serenity/blob/master/Userland/Services/WindowServer/WindowServer.ipc>
- <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>
  (flip model, DirectFlip/iFlip tiers)
- <https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain>
  (composition vs direct scanout)

---

## 4. File system

### Current state

- `kernel/vfs/hypefs.cc`: in-memory flat-fs — 192 entries max, three fixed
  disks S(2 MiB)/U(384 KiB)/T(128 KiB), `fs_write` exists but writes only RAM.
- `kernel/vfs/hypeblob.cc`: persistent store = a custom `HYPEDATA` blob at
  **fixed LBA 4129**, read once at boot via ATA PIO and expanded into memfs.
  Read-only on disk — no write-back at all; all runtime file changes are lost.
- `kernel/ata.cc`: PIO reads only (`cmd_read_sectors 0x20`), master LBA28,
  poll-based, no write command (0x30), no `FLUSH CACHE` (0xE7), no IDENTIFY
  (0xEC), no sector cache, no IRQ-driven I/O.
- `kernel/filesystem.h`: an `IFileSystem`/`FileOperations` interface sketch —
  present but essentially unused; hypefs doesn't implement it.

### What real OSes do

- Hobby-OS canonical order: ATA PIO (have) -> **ATA IDENTIFY + write + flush**
  -> sector/block cache -> FAT32 on-disk -> AHCI (PRDT DMA, NCQ optional) or
  **NVMe** (simpler than AHCI: two rings, no ATA command set at all).
- FAT32 is the de-facto "first real fs": boot sector BPB -> FAT chain walk ->
  8.3 + LFN directory entries; write path needs FAT mirror + free-cluster
  tracking + `FSInfo` sector. exFAT is arguably *simpler* (no FAT mirror, bitmap
  allocator, one dir-entry set per file) but 64 KiB+ cluster assumptions and
  fewer QEMU-toolchain precedents make FAT32 the safer first target.
- Journaling is out of scope for the first disk fs; the realistic durability
  ladder is: write-through + `FLUSH CACHE` on unmount/sync -> periodic lazy
  write-back -> a tiny "sector journal" for the HYPEDATA blob only. fsck for
  FAT = cross-check FAT chains vs dir entries, fix free counts — a weekend
  tool in `tools/` operating on the disk image.

### Recommended changes

1. `ata.cc`: add `ata_identify` (0xEC; model/serial/LBA48 capacity),
   `ata_pio_write` (0x30), `ata_flush` (0xE7), both-drive select, 48-bit LBA
   variant. ~1 file, unlocks all persistence.
2. `hypeblob.cc`: write-back path — `hypeblob_sync()` re-serializes dirty
   memfs entries to the HYPEDATA region + `ata_flush`; call from svc shutdown
   and an explicit `sync` path. Blob format already exists, so this is the
   cheapest possible persistence.
3. `kernel/fs/fat32.{h,cc}` (new): read-only mount of a FAT32 partition on the
   same disk (mkimage puts one at a fixed LBA — extend `tools/mkimage.py`),
   then write. Exposes the `hype::fs::IFileSystem` interface so it finally
   gets used.
4. `kernel/blockdev.h` (new): `read_sectors/write_sectors` vtable + sector
   cache (64-256 entries, LRU, dirty bits, `blockdev_sync`) in front of
   ATA/AHCI/NVMe alike — filesystems bind to blockdev, not to ATA.
5. Defer journaling/AHCI/NCQ per roadmap; NVMe is the better P2 (see §table).

Sources:
- <https://wiki.osdev.org/ATA_PIO_Mode> (IDENTIFY/write/flush command set)
- <https://wiki.osdev.org/FAT> + Microsoft FAT spec
  <https://www.win.tue.nl/~aeb/linux/fs/fat/fatgen103.pdf>
- <https://wiki.osdev.org/AHCI>, NVMe links already collected in
  `os-core-roadmap.md` §1

---

## 5. VFS layer

### Current state

- `hypefs.h` API is *path-in/path-out*: every op re-parses
  `HYPEOS://D/a/b/c` (`hypepath.cc`, max 8 components x 32 chars) and linear-
  searches a 192-entry table. There is **no open-file object, no fd table, no
  offset** — `fs_read` always reads whole file into a caller buffer.
- No mount table: disks S/U/T are compiled-in constants. `IFileSystem` exists
  but nothing registers into a namespace.
- No dentry/inode cache, no rename/link/unlink-at-path-level granularity, no
  async or readiness API; `functionemitkeys`-style subscribers exist for input
  only.
- Wait queues (`wait_queue.h`, 16-slot task-id array) exist but aren't wired
  to blocking file I/O; all I/O is synchronous CPU copies anyway.

### What real OSes do

- Linux: path -> dentry cache -> inode -> file object (offset, flags) -> fd
  table per process; `struct file_operations` vtable per fs (already sketched
  here in `filesystem.h`).
- SerenityOS: `FileDescription` + global `Inode` table + per-process fd array;
  `VirtualFileSystem` mounts `(fs, inode)` pairs at paths.
- Readiness: poll/epoll-style — fd reports `can_read/can_write` via fs-level
  predicate; blocking reads park the thread on a wait queue until the device
  IRQ wakes it.

### Recommended changes

1. `kernel/vfs/mount.{h,cc}` (new): mount table {disk letter -> IFileSystem*};
   `hypefs` becomes the memfs implementation of it; FAT32/blob plug into the
   same table — this is the seam §4-3 needs.
2. `kernel/vfs/fd.{h,cc}` (new): `vfs_open(path, flags) -> fd`, fd table
   (fixed 64), `vfs_read/vfs_write/vfs_seek/vfs_close` with per-fd offset —
   required before any streaming consumer (audio files, network sockets,
   pipes).
3. `hypefs.cc`: add `fs_rename(from,to)` (trivial in memfs: swap name fields)
   + `fs_link`-free semantics documented; keep `FS_SYS` guard.
4. Blocking I/O, when disk IRQs arrive: make `wait_queue` hold a sleeper
   callback/`Task*` rather than raw task ids, and add
   `wait_queue_sleep_current()`/`wake` — it becomes the futex/poll substrate
   (futex = wait queue keyed by address; you already have all parts).
5. Dentry cache: defer — 192-entry linear scan is fine until FAT32 mounts;
   add a per-mount name->inode hash then, not earlier.

Sources:
- <https://wiki.osdev.org/VFS>
- SerenityOS `Kernel/FileSystem/VirtualFileSystem.cpp` design (mount table,
  FileDescription/fd split): <https://github.com/SerenityOS/serenity/tree/master/Kernel/FileSystem>

---

## 6. Protocols / network

### Current state

- **Nothing.** `kernel/chips/virtio.cc` only maps virtio PCI device IDs to
  names (net legacy 0x1000/modern 0x1041, input 0x1048 — notably already
  listed). No virtqueue code, no Ethernet, no ARP/IP.

### What hobby OSes actually ship first

- Two viable first NICs on QEMU: **e1000** (82540EM, `-nic model=e1000` —
  documented register interface, descriptor rings, ~800 lines) and
  **virtio-net** (legacy 0x1000 or modern 0x1041 — needs virtqueue transport
  but no per-vendor NIC quirks; also exercises the MSI-X code from §1 and
  shares the virtqueue core with virtio-blk/input/gpu later). RTL8139 is the
  historical easy one but is a dead-end (fast Ethernet, legacy QEMU).
- Minimal stack ladder: Ethernet rx/tx -> **ARP** (req/reply + 4-entry cache)
  -> IPv4 header + ICMP echo reply ("it pings!") -> **UDP** + **DHCP**
  discover/offer/request/ack -> DNS -> TCP (7-state machine, retransmit,
  MSS/pmtu ignore on QEMU user-net) -> sockets/fd plumbing (§5).
- QEMU user-mode net (`-netdev user`) gives DHCP/DNS at 10.0.2.x for free —
  no host networking setup needed; virtio-net there is zero-config.

### Recommended changes

1. `kernel/net/` (new): `eth.{h,cc}` (frame rx/tx interface + 6-byte MAC),
   `arp.{h,cc}`, `ipv4.{h,cc}`, `icmp.cc` (ping reply), `udp.cc`, `dhcp.cc`.
2. `kernel/chips/virtio/` → promote to real driver dir: `virtio_pci.cc`
   (legacy I/O-port transport first — simplest: common cfg at BAR0 I/O —
   then modern cap-list transport), `virtqueue.{h,cc}` (split vring
   desc/avail/used, no indirect features), `virtio_net.cc` (device id 1,
   12-byte `virtio_net_hdr`, 2 queues, 64-entry RX refill). Feature-negotiate
   VERSION_1 + MAC only; compute checksums in software (no CSUM offload).
3. Poll-driven first (main loop already pumps every tick); interrupt-driven
   via MSI-X (§1-4) once LAPIC/IOAPIC are up — virtio is the natural first
   MSI-X consumer.
4. `socket`-lite: `udp_send/udp_recv` + a `net_poll()` pump hook; defer BSD
   socket/fd surface until TCP.

Sources:
- VirtIO 1.2 spec: <https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html>
- Minimal modern virtio-net driver walkthrough (feature bits, queue sizing):
  <https://kannaka-labs.github.io/kannaka-library/quantumos/docs/adr/0024-virtio-core-and-transports.html>
- QEMU-side virtio-net RX path:
  <https://www.openeuler.org/en/blog/xinleguo/2020-11-23-Virtio_Net_Technology>
- <https://wiki.osdev.org/Virtio>, <https://wiki.osdev.org/Intel_8254x> (e1000
  alternative), DHCP RFC 2131 <https://www.rfc-editor.org/rfc/rfc2131>

---

## 7. Input / HID

### Current state

- PS/2 keyboard via IRQ1 (`idt.cc:87-91`) -> set-1 scancode decoder
  (`scancode.cc`) -> evdev-style `InputEvent` emitter with subscribers
  (`functionemitkeys.h:9-23` — already a clean abstraction).
- PS/2 mouse: `msaux.cc` 3-byte packets, polling `cursor_poll()` in the main
  loop; no IRQ12 handler visible — mouse bytes are polled, which loses bytes
  under load. No scroll wheel (4-byte Intellimouse packet), no absolute
  positioning.
- No USB at all; `chips/virtio.cc` lists virtio-input 0x1048 but nothing
  implements it.

### What's worth it, honestly

- **PS/2 is fine for QEMU + all pre-2015 hardware.** USB HID is only needed
  for real modern hardware (post-2015 machines may lack PS/2 emulation) —
  it's an insurance policy, not a feature. Community consensus and the
  Impossible-OS experience both converge on: xHCI-only (QEMU `-device
  qemu-xhci` covers USB1/2/3 devices), control+interrupt transfers,
  boot-protocol HID only (skip report-descriptor parsing initially),
  feeding reports into the *same* `emit_key_event`/pointer layer PS/2 uses.
- Cheaper intermediate wins: IRQ12 mouse handler + Intellimouse init
  (0xF3 200/100/80 sequence) for scroll+4-byte packets; virtio-input
  (`-device virtio-keyboard-pci,virtio-mouse-pci` — QEMU emulates them and
  the driver is a tiny virtqueue consumer, ~200 lines once §6-2 exists).
  Virtio-tablet-pci additionally gives *absolute* cursor position — no more
  mouse-capture drift vs the QEMU window.

### Recommended changes

1. `idt.cc`/`msaux.cc`: move mouse to IRQ12; add Intellimouse init + 4-byte
   packet parse (wheel) — one afternoon, real UX win.
2. `virtio_input.cc` after virtqueue core (§6-2): keyboard + mouse; bonus:
   virtio-tablet absolute mode.
3. P2: `kernel/usb/` — `xhci.cc` (DCBAAP, command/event rings, control Xfer)
   + `usb_hid.cc` boot-protocol path feeding `functionemitkeys`. Real-hw only
   benefit; keep PS/2 as the primary path.

Sources:
- <https://forum.osdev.org/viewtopic.php?t=40913> (OSDev consensus: xHCI is
  the sane single target; UHCI not worth it)
- Impossible OS USB stack/HID-boot-protocol notes:
  <https://impossibleos.co/docs/hardware/usb-stack.html>,
  <https://impossibleos.co/docs/boot/usb-hid-boot-protocol.html>
- xHCI 1.2 spec link already in `os-core-roadmap.md` §1

---

## 8. HypeAIGDXF — game-driver subsystem proposal

Goal: a *mini-DirectX for HypeAI OS* — one stable `hype::gdxf::*` surface a
game links against, hiding the loop timing, input queues, present path, and
low-latency audio that this kernel already half-has (`hasio` streams,
`InputEvent` bus, Canvas). Designed to degrade gracefully: works today on
PIT+PS/2+memcpy-present, transparently upgrades to APIC-timer timing,
IRQ12/virtio-input and flip-present as §2/§3/§7 land.

### Design anchors (from real systems)

- **Fixed timestep + accumulator + interpolation** (Gaffer "Fix Your
  Timestep!"): sim advances in constant `dt`, renderer gets `alpha` for
  interpolation; frame-time clamp (~0.25 s) prevents spiral-of-death.
- **SDL2 shape**: `SDL_PollEvent` + `SDL_GetKeyboardState` dual API —
  polled *state* for gameplay, queued *events* for UI/text. Mirror that
  split: `gdxf_key_down()` (state) vs the existing subscriber bus (events).
- **raylib shape**: `InitWindow/BeginDrawing/EndDrawing` — the API owns the
  loop; beginners get `gdxf_run(callbacks)`, experts get
  `gdxf_begin_frame/end_frame`.
- **WebGPU/DXGI shape**: acquire surface -> draw -> present-with-dirty-rects
  -> the runtime decides blt-vs-flip (`gdxf_present` maps to today's
  `write_frame`, tomorrow's flip chain).
- **Audio**: wrap `hasio` (32-512-frame periods, ≤8 ms budget already coded)
  behind a simpler write-side stream API.
- **Sprites**: dirty-rect-aware batched clipped blits + a tilemap fast path —
  the two primitives 90% of 2D games need.

### Proposed API surface (repo style: freestanding, noexcept, fixed caps)

File layout: `kernel/gdxf/gdxf.h` (public), `gdxf.cc` (core loop),
`gdxf_surface.cc` (back buffer + present), `gdxf_input.cc`,
`gdxf_audio.cc`, `gdxf_sprite.cc`, `gdxf_time.cc`.

```cpp
namespace hype::gdxf {

// ---- types ----
enum class PixelFormat : hcpp::u8 { XRGB32, RGB24 };
enum class VSyncMode  : hcpp::u8 { Uncapped, TickLock, Retrace }; // Retrace = future flip
enum class TexFlags   : hcpp::u16 { None=0, Alpha=1, FlipX=2, FlipY=4 };

struct Surface { hcpp::u8* pixels; hcpp::u32 width, height, pitch_bytes;
                 PixelFormat format; };                       // borrow-only, valid until present
struct FrameTiming { hcpp::u64 frame; hcpp::u64 t_us;
                     hcpp::u32 delta_us; float dt; float alpha; };
struct MouseState  { hcpp::i32 x, y; hcpp::u8 buttons; hcpp::i8 wheel; };
struct GamepadState{ bool connected; hcpp::u16 buttons;
                     hcpp::i16 lx, ly, rx, ry; hcpp::u8 lt, rt; };
struct AudioFormat { hcpp::u32 rate; hcpp::u8 channels; };    // s16 interleaved
struct GameCallbacks {
    void (*init)(void* ctx) noexcept;
    void (*update)(float dt, void* ctx) noexcept;             // fixed-dt sim step
    void (*render)(Surface&, float alpha, void* ctx) noexcept;
    void* ctx;
};

// ---- lifecycle / loop ----
bool gdxf_initialize(hcpp::graphics::Framebuffer& scanout) noexcept;
hcpp::u32 gdxf_run(const GameCallbacks&) noexcept;            // engine-owned loop, returns exit code
bool gdxf_begin_frame(FrameTiming&) noexcept;                 // pumps input, computes dt/alpha
void gdxf_end_frame() noexcept;                               // rate-limit + present
void gdxf_request_quit() noexcept;
void gdxf_set_tick_rate(hcpp::u32 hz) noexcept;               // fixed dt, default 60
void gdxf_set_vsync(VSyncMode) noexcept;

// ---- surface / present ----
bool gdxf_surface_acquire(Surface&) noexcept;                 // back buffer borrow
void gdxf_present(const hype::ui::Rect* dirty, hcpp::u32 n) noexcept; // blt today, flip later
hcpp::u32 gdxf_surface_count() noexcept;                      // chain depth (1 today, 2+ with flip)
bool gdxf_fullscreen_direct() noexcept;                       // "DirectFlip-equivalent" engaged?

// ---- input (per-frame state snapshot; events stay on functionemitkeys) ----
bool gdxf_key_down(hcpp::u16 key_code) noexcept;
bool gdxf_key_pressed(hcpp::u16 key_code) noexcept;           // edge since last frame
bool gdxf_mouse(MouseState&) noexcept;
hcpp::u8 gdxf_gamepad_count() noexcept;                       // 0 until virtio-input/gamepad
bool gdxf_gamepad_state(hcpp::u8 index, GamepadState&) noexcept;

// ---- time ----
hcpp::u64 gdxf_ticks() noexcept;            // monotonic, backend = APIC-timer/TSC/PIT
hcpp::u64 gdxf_ticks_per_second() noexcept;
hcpp::u64 gdxf_time_us() noexcept;
void     gdxf_sleep_us(hcpp::u64 us) noexcept;                // hlt-sleep, not spin

// ---- audio (over hasio) ----
hcpp::u32 gdxf_audio_open(const AudioFormat&, hcpp::u32 period_frames) noexcept;
bool     gdxf_audio_write(hcpp::u32 stream, const hcpp::i16* frames, hcpp::u32 count) noexcept;
hcpp::u32 gdxf_audio_queued_frames(hcpp::u32 stream) noexcept;
void     gdxf_audio_close(hcpp::u32 stream) noexcept;

// ---- sprites / tiles (batched clipped blit into acquired surface) ----
hcpp::u32 gdxf_tex_create(hcpp::u32 w, hcpp::u32 h, const hcpp::u32* argb) noexcept; // cap 64
void      gdxf_tex_destroy(hcpp::u32 tex) noexcept;
void      gdxf_sprite_batch_begin() noexcept;
void      gdxf_sprite_draw(hcpp::u32 tex, hcpp::i32 x, hcpp::i32 y, TexFlags) noexcept;
void      gdxf_sprite_draw_ex(hcpp::u32 tex, hype::ui::Rect src, hype::ui::Rect dst, TexFlags) noexcept;
void      gdxf_sprite_batch_end() noexcept;                   // one clipped pass, fills dirty list
void      gdxf_tilemap_draw(hcpp::u32 tex, hcpp::u32 tile_w, hcpp::u32 tile_h,
                            const hcpp::u8* map, hcpp::u32 map_w, hcpp::u32 map_h,
                            hcpp::i32 cam_x, hcpp::i32 cam_y) noexcept;

// ---- debug ----
hcpp::u32 gdxf_frame_us() noexcept;          // rolling avg, for an fps overlay
void      gdxf_set_fps_overlay(bool) noexcept;

}  // namespace hype::gdxf
```

Dependencies mapping (drives the ticket order):
`gdxf_time` ← §2-3 timer ladder · `gdxf_input` ← existing
`scancode`/`msaux`/`functionemitkeys` (+virtio-input later) ·
`gdxf_present` ← §3-1/3-4 back-buffer+present seam · `gdxf_audio` ← hasio
wrap · `gdxf_sprite` ← Canvas-style clipped blit on `Surface`.

---

## 9. Misc hardening

| Area | Current | Recommended |
|---|---|---|
| Panic | exceptions print to serial and **return → re-fault storm** (`idt.cc:39-45`) | `kernel/panic.cc` BSOD: regs+CR2+error decode on fb+serial, cli+hlt (P0) |
| Logging | `serial::write*` raw strings, no levels/tags | `kernel/klog.h`: `klog(KlogLevel::Info,"pci","...")` + level mask + tag; keep serial sink (P0) |
| Watchdog | none | HPET or LAPIC-timer NMI watchdog: armed during boot stages, fires -> panic dump instead of silent hang (P1) |
| Entropy/RNG | `hcpp/rng.cc`: RDRAND else **raw TSC** — weak | Add entropy pool: mix TSC⊕PIT-jitter⊕IRQ1/12 timing⊕RDRAND through SHA-256 (have it) into a chacha-ish counter; gate `rng_backend` on CPUID (P1, cheap since sha256 exists) |
| SMP | MADT counts CPUs (`madt.cc`), zero AP startup | Roadmap §1 covers INIT-SIPI-SIPI; prerequisites here: LAPIC MMIO enable (§2-4), per-CPU idle stack, trampoline at <1 MiB, spinlock audit (locks exist: `spinlock.h`, `mutex.h` w/ recursion) (P2) |
| Concurrency | wait_queue(16 ids), mutex(recursive), semaphore, spinlock all exist; scheduler = 2-thread **cooperative selftest** (`sched/scheduler.cc`) not preemptive | Real preemptive scheduler on the §2-3 APIC-timer tick: thread table, runqueue, timer preemption → context.asm switch already proven; futex = wait_queue keyed by address (P1) |
| syscall | `syscall.h` dispatch table exists, no userspace | keep; route GDXF host calls through it later |

---

## 10. Prioritized improvement table

### P0 — cheap, high value (do first; mostly ≤1 day each)

| # | Subsystem | Change | Files | ~Effort | Benefit |
|---|---|---|---|---|---|
| P0-1 | Panic | Exception→BSOD dump + cli/hlt; stop the re-fault storm | `kernel/panic.{h,cc}` (new), `kernel/interrupts/idt.cc` | 1 d | Debuggability of everything below |
| P0-2 | Logging | `klog` levels/tags over serial | `kernel/klog.h`, `kernel/serial.*` | 0.5 d | Triage boot + driver logs |
| P0-3 | PCI scan | Bridge recursion, fn-skip via header-type, 256 devices, BAR+IRQ cache in `PciDevice` | `kernel/pci/pci.cc`, `pci.h` | 1 d | Faster boot, no silent device loss |
| P0-4 | PCI config | BAR sizing helper + Command-register setters (bus-master!) | `kernel/pci/pci_io.cc`, `pci_io.h` | 0.5 d | Unblocks every future DMA driver |
| P0-5 | ATA | IDENTIFY + PIO write + FLUSH CACHE + LBA48 | `kernel/ata.cc`, `ata.h` | 1 d | Persistence prerequisite |
| P0-6 | VFS persist | `hypeblob_sync()` write-back + flush; hook sync points | `kernel/vfs/hypeblob.cc` | 1 d | Files survive reboot |
| P0-7 | Timing | RTC read (0x70/0x71); CPUID feature struct | `kernel/time/rtc.cc`, `kernel/arch/x86/cpuid.cc` (new) | 1 d | Wall clock, feature gating |
| P0-8 | Input | IRQ12 mouse + Intellimouse wheel packets | `kernel/interrupts/idt.cc`, `kernel/input/msaux.cc` | 0.5 d | Scroll wheel, no lost bytes |

### P1 — medium (1-7 days)

| # | Subsystem | Change | Files | ~Effort | Benefit |
|---|---|---|---|---|---|
| P1-1 | Interrupts | LAPIC MMIO + IOAPIC routing from parsed MADT/ISO; PIC masked | `kernel/acpi/ioapic.cc`, `acpi/apic.cc`, `interrupts/idt.cc` | 2-3 d | Real IRQ routing; MSI-X usable |
| P1-2 | Timing | TSC calib via PIT/HPET; LAPIC timer replaces IRQ0 tick | `kernel/time/tsc.cc`, `apic_timer.cc`, `acpi/hpet.cc` | 2 d | µs timing (audio, games, sched) |
| P1-3 | PCIe | MCFG parse + ECAM access path | `kernel/acpi/mcfg.cc`, `pci/*` | 1-2 d | Extended cfg space, PCIe devices |
| P1-4 | PCIe | MSI-X table/PBA programming | `kernel/pci/pci_msix.{h,cc}` | 1 d | virtio/NVMe interrupts |
| P1-5 | Render | Back buffer + damage rects; `wm_compose`; cursor composited | `kernel/ui/region.h`, `win/winman.cc`, `win/deskman.cc`, `input/cursprite.cc` | 2-3 d | 10-100x less repaint work |
| P1-6 | VFS | Mount table + fd table + per-fd offset | `kernel/vfs/mount.{h,cc}`, `fd.{h,cc}`, `hypefs.cc` | 3 d | Streaming I/O, multi-fs |
| P1-7 | VFS | `fs_rename`; wait_queue→sleeper API (futex-ready) | `kernel/vfs/hypefs.cc`, `kernel/wait_queue.*` | 1 d | Real fs semantics, blocking IO |
| P1-8 | Sched | Preemptive scheduler on APIC-timer tick (threads, runqueue) | `kernel/sched/*`, `arch/x86/context.asm` | 5-7 d | Everything stops being a giant main-loop |
| P1-9 | GDXF | MVP: loop+timestep, input state, surface/present, audio wrap, sprite+tile blits | `kernel/gdxf/*` (new) | 5-7 d (after P0/P1-2/5) | Playable games; forces the right seams |
| P1-10 | Watchdog | HPET/LAPIC NMI watchdog during boot stages | `kernel/watchdog.cc`, bootseq | 1 d | Hangs become panics |

### P2 — large (≥1 week)

| # | Subsystem | Change | Files | ~Effort | Benefit |
|---|---|---|---|---|---|
| P2-1 | Storage | FAT32 ro→rw mount via blockdev+sector cache | `kernel/fs/fat32.*`, `kernel/blockdev.*`, `tools/mkimage.py` | 5-8 d | Real on-disk fs, tooling compatibility |
| P2-2 | Storage | NVMe (preferred over AHCI — simpler) w/ MSI-X | `kernel/nvme/*` | 1-2 wk | Fast async disk, replaces ATA |
| P2-3 | Net | virtio-net (virtqueue core + legacy PCI transport) + ARP/ICMP/UDP/DHCP | `kernel/chips/virtio/*`, `kernel/net/*` | 2-3 wk | First packets; basis for all net |
| P2-4 | Net | TCP + socket-lite over §5 fd table | `kernel/net/tcp.cc` | 2 wk | Real protocols |
| P2-5 | Input | virtio-input (kbd/mouse/tablet abs-pos) | `kernel/chips/virtio/virtio_input.cc` | 3-5 d | Absolute cursor; gamepad path for GDXF |
| P2-6 | USB | xHCI + HID boot protocol | `kernel/usb/xhci.*`, `usb_hid.cc` | 3-4 wk | Real-hardware input (insurance) |
| P2-7 | SMP | INIT-SIPI-SIPI AP bring-up, per-CPU state, IPI shootdowns | `kernel/smp/*`, trampoline asm | 2 wk | Real multicore for codecs/render |
| P2-8 | Boot | BootInfo v3 (RSDP field, 24B E820, module list) → UEFI/multiboot2 option | `boot/stage2.asm`, `kernel/boot_info.hpp` | 1 wk | Non-BIOS boot path |
| P2-9 | Display | bochs-display/virtio-gpu modeset → true flip present | `kernel/display/*` | 2 wk | Vsync, tear-free (see display-roadmap) |
| P2-10 | PCI | Ext-cap walk (AER), hot-plug via GPE/`_OSC` | `kernel/pci/*`, AML interpreter (roadmap) | far future | Real-hw completeness |

### Suggested sequencing note

P0-1..8 → P1-1/P1-2 (interrupt+timing base) → P1-5 (damage) → **P1-9 HypeAIGDXF
MVP** (largest user-visible win that exercises all prior work) → P1-6..8 →
P2-1/2-3 (FAT32, virtio-net) → rest.
