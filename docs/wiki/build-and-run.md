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

## Hybrid BIOS+UEFI image (2026-11-28, `devin/p22-hybrid`)

`make` (or `make img`/`make iso`/`make usb`) builds ONE image,
`build/hypeai-os.img`, that boots both legacy BIOS/CSM and UEFI-only
machines — the old separate `hypeai-os-uefi.img` is gone (`make uefi` is
now an alias, and `tools/mkesp.py` only remains as a shim for the old
filename). `tools/mkimage.py` writes the whole disk directly — no
mtools/parted needed. Layout:

- **LBA 0** — stage1 boot code + a hybrid-MBR partition table (bytes
  446..509): `0xEE` protective entry for LBA 1..33, `0xEF` entry for the
  ESP, and an active `0xDA` entry covering stage2+kernel+HYPEDATA.
- **LBA 1..33** — primary GPT header + partition entries.
- **LBA 34..65** — stage2 (moved off LBA 1; `boot/stage1.asm`
  `STAGE2_LBA=34`).
- **LBA 66..8257** — `kernel.bin` (`boot/stage2.asm` `KERNEL_LBA=66`).
- **LBA 8258..16449** — HYPEDATA blob (`hypeblob_data_lba=8258` in
  `kernel/vfs/hypeblob.h`), then the crash-dump scratch at 16450..16581,
  all inside the "HypeAI data" GPT partition that runs to the ESP.
- **LBA 18432..** — 64 MiB FAT32 ESP with `EFI/BOOT/BOOTX64.EFI` +
  `EFI/HYPEAI/KERNEL.BIN`.
- **End** — backup GPT entry array + header.

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

Flash `build/hypeai-os.img` to the stick (Rufus "DD Image" / balenaEtcher /
`dd`). The one image covers everything: BIOS/CSM (Legacy) boot works, and
UEFI-only laptops boot with Secure Boot disabled. Windows will show a
~64 MB FAT32 partition on the stick — that is the ESP, it is normal.

### QEMU/OVMF verification

OVMF firmware ships with QEMU at
`C:\Program Files\qemu\share\edk2-x86_64-code.fd`. Load it as pflash:

```bash
cp build/hypeai-os.img /c/Users/alino/hypeai-os.img   # ASCII path!
"/c/Program Files/qemu/qemu-system-x86_64.exe" \
  -drive if=pflash,format=raw,readonly=on,file='C:\Program Files\qemu\share\edk2-x86_64-code.fd' \
  -drive format=raw,file=/c/Users/alino/hypeai-os.img \
  -serial file:/c/Users/alino/qemu-uefi-serial.log -display none -no-reboot -m 256
```

(`-bios` fails on Windows; use the pflash drive. `make run-uefi` exists but
the Cyrillic repo path needs the copy step first.)

Verified: the same `hypeai-os.img` boots both ways — BIOS serial reaches
`HYPEAI OS BOOT OK`, `stages=14 completed=14`; OVMF serial shows
`HypeBoot-UEFI: kernel loaded` then `HYPEAI OS BOOT OK`,
`fb mode=1920x1080`, `blob ata=ok` + `blob files=48` (HYPEDATA read from
LBA 8258 over ATA), `session auth=ok`, `gdxf fps=~11`.
