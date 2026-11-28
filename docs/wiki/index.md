XX# HypeAI OS wiki
XX
XXHistorical record of decisions, environment quirks and subsystem knowledge. Date every claim; cite commits for code claims.
XX
XX## Environment
XX
XX- [Build & run on this machine](build-and-run.md) — MSYS2/clang toolchain commands, QEMU invocation, path pitfalls
- [USB / VirtualBox / real hardware](run-hardware.md) — dd-образ на флешку, make vdi, настройки VM
XX
XX## Programs
XX
XX- [HypeAIFont + first UI](hypeai-font-ui.md) — spec #3; also holds program-level notes + index for #13/#23
XX- [Hello auth + audio](hello-audio.md) — spec #13: sqlite-in-kernel, Hello enroll/auth, HDA driver + PCM playback, login UI
XX- [CodecApps](codecapps.md) — spec #23: codec registry/arena, image+audio decoders, deferred-format policy
- [Payload packs](payload-packs.md) — ticket #74/spec #71: HYPEDATA blob mount routing, wallpaper/docs/font/app packs
- [Core OS P0 + HypeAIGDXF (P14)](core-os-p14.md) — klog/panic, PCI scan, ATA-write persistence, RTC/CPUID, mouse IRQ12+wheel, damage compositor, dark theme, gdxf
- [Platform + desktop shell (P15)](core-os-p15.md) — HPET/TSC, LAPIC/IOAPIC, virtio-net+DHCP; taskbar/Start/icons/ctx-menus/snap/alt-tab/tray/lock/power
- [Storage/USB/VFS + P17](core-os-p16-p17.md) — PCIe ECAM/MSI-X, AHCI, xHCI+HID, VFS fd table, NVMe; RevServices/RevDBG, SMP, DHCP fix, FAT32
XX
XX## Research
XX
XX- `../research/runtime-failures.md` — root cause of dead runtime stages (no .init_array execution)
XX- `../research/hello-audio.md` — investigation for spec #13 (QEMU devices, HDA/AC97, ALSA-style API)
XX- `../research/codecapps.md` — investigation for spec #23 (format magic, port-friendly PD/MIT decoder libs)
- `../research/core-os-improve.md` — P14 audit: P0-P2 priority table the P14-P17 tickets implement
- `../research/desktop-shell.md` — P15 desktop-shell research (taskbar/icons/snap/lock/power)
- [display](display.md) — P1 display engine: modeset/DPI/CTA/multi-display/scaler/capture subsystems
- [boot-type](boot-type.md) — P8: native TTF font engine, gfx shadows/elevation, hyperui restyle, bootsplash/bootseq
