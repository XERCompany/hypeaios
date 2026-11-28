# Build & run on this machine

As of 2026-11-26 (commit `068d52f`):

## Toolchain

- MSYS2 lives at `C:\tools\msys64`. Installed packages: `gcc`, `nasm`, `make`, `xorriso`, `python` (msys repo) and `clang`/`lld` (`mingw-w64-ucrt-x86_64-*`).
- **Do not use msys `g++`/`ld`** — they are PE/COFF tools and cannot link the ELF64 kernel. On 2026-11-26 we switched to clang:

```bash
/c/tools/msys64/usr/bin/bash.exe -lc 'export PATH=/ucrt64/bin:$PATH; cd <repo> && make iso'
```

- As of commit `a5f18c2` (2026-11-26) plain `make iso` works: the Makefile defaults `CXX=clang++`, `CC=clang`, `LD=clang++ --target=x86_64-unknown-none-elf -fuse-ld=lld` when the variables come from make's built-in defaults (the earlier `CC ?= clang` never fired because GNU make predefines `CC=cc`). Explicit `CXX=`/`CC=`/`LD=` overrides are still honored.
- **Pitfall:** stale `build/` objects from an aborted g++ build are COFF and break the lld link with `unknown file type` — run `rm -rf build` once after switching toolchains.

- `kernel/runtime/builtins.cc` (added 2026-11-26) provides `memset/memcpy/memmove/memcmp`; clang emits calls to them in freestanding code (e.g. `s = {}`). Keep this file.
- `tools/mkiso.py` must pass relative paths to xorriso (it cannot resolve `C:/...` paths).

## QEMU

QEMU was installed via `choco install qemu` (2026-11-26) → `C:\Program Files\qemu\qemu-system-x86_64.exe`.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -drive format=raw,file=<img> -serial stdio -display none -no-reboot
```

**Pitfall:** QEMU on Windows mangles non-ASCII paths. The repo lives under `...\OneDrive\Документы\` (Cyrillic), so QEMU cannot open images inside the repo tree. Copy `build\hypeai-os.img` to an ASCII path (e.g. `C:\Users\alino\hypeai-os.img`) before booting, or run QEMU from a shell that passes the raw image bytes. Worktrees must also live under ASCII paths (e.g. `C:\Users\alino\hypeaios-wt-*`).

## Headless screenshots (2026-11-26)

Run QEMU with `-qmp tcp:127.0.0.1:4444,server,nowait` and `-serial file:<log>` (note: `-serial file:` may not create the log on Windows — capture stdout instead). Then drive QMP over a raw socket with sendall/recv (line-buffered `makefile` blocks — use `recv` with explicit timeouts): `qmp_capabilities`, then `human-monitor-command` → `screendump C:/path/out.ppm`. PPM→PNG conversion is a ~15-line zlib script. Serial log via file: is unreliable; `-serial stdio` in a foreground shell works.

## Boot state (2026-11-26)

Serial log shows `HYPEAI OS BOOT OK`, framebuffer 1024x768x24 EDID-parsed, but several runtime stages report failures (`interrupts=failed`, `vmm=failed`, `kernel threads=failed`, PMM sees no memory) — pre-existing kernel bugs, not build problems.

## UEFI boot (2026-11-28, `devin/p22-uefi`)

`make uefi` builds `build/hypeai-os-uefi.img`: a GPT disk with a FAT32 ESP
(`EFI/BOOT/BOOTX64.EFI` + `EFI/HYPEAI/KERNEL.BIN`) plus the HYPEDATA blob at
the same fixed LBA 4129 the BIOS image uses, so the kernel block layer finds
the payload identically. No external tools needed — `tools/mkesp.py` writes
the GPT/FAT32 directly; `tools/mkimage.py` exports `blob_region()` for reuse.

- `boot/uefi/bootx64.cc` is freestanding C++ compiled for
  `x86_64-pc-windows-msvc` (the MS x64 ABI is the UEFI calling convention) and
  linked by `lld-link -subsystem:efi_application -entry:efi_main`. Minimal
  UEFI protocol definitions live in `boot/uefi/efi.hpp` — no gnu-efi.
- The loader mirrors the stage2 contract exactly: kernel at `0x100000`,
  BootInfo at `0x5000`, e820 at `0x5500`, stage2's GDT at `0x5800`, 4 GiB
  identity page tables at `0x70000`, hand-off `jmp 0x100000` with
  `rdi=0x5000`, `rsp=0x90000`, CS=0x18/DS=SS=0x10. The GDT far-jump is
  **required** — `kernel/interrupts/idt.cc` hardcodes selector `0x18` for
  every gate and the kernel never installs a BSP GDT.
- GOP mode pick: 1920x1080 preferred (stage2's `vbe_prefs` order), else the
  largest mode ≤3840x2160. `BootInfo.framebuffer` is u32, so a framebuffer
  above 4 GiB clears flags bit0 → kernel VGA-text fallback.
- Secure Boot must be **off** — the image is unsigned. Runtime services are
  not used after `ExitBootServices` (e820 marks them reserved).

### Flash to USB / run on hardware

Flash `build/hypeai-os-uefi.img` to the stick the same way as the BIOS image
(Rufus "DD Image" / balenaEtcher / `dd`). Boot with Secure Boot disabled.
UEFI-only laptops work; the BIOS/CSM path stays intact.

### QEMU/OVMF verification

OVMF firmware ships with QEMU at
`C:\Program Files\qemu\share\edk2-x86_64-code.fd`. Load it as pflash:

```bash
cp build/hypeai-os-uefi.img /c/Users/alino/hypeai-os-uefi.img   # ASCII path!
"/c/Program Files/qemu/qemu-system-x86_64.exe" \
  -drive if=pflash,format=raw,readonly=on,file='C:\Program Files\qemu\share\edk2-x86_64-code.fd' \
  -drive format=raw,file=/c/Users/alino/hypeai-os-uefi.img \
  -serial file:/c/Users/alino/qemu-uefi-serial.log -display none -no-reboot -m 256
```

(`-bios` fails on Windows; use the pflash drive. `make run-uefi` exists but
the Cyrillic repo path needs the copy step first.)

Verified: serial reaches the desktop — `fb mode=1920x1080`,
`memory free=104MB e820=32 usable=9`, `stages=14 completed=14 failed=0`,
`blob ata=ok` + `blob files=48` (HYPEDATA read from LBA 4129 over ATA),
`session auth=ok`, `cursor poll=active`, `gdxf fps=~11`.
