# Build & run on this machine

As of 2026-11-26 (commit `068d52f`):

## Toolchain

- MSYS2 lives at `C:\tools\msys64`. Installed packages: `gcc`, `nasm`, `make`, `xorriso`, `python` (msys repo) and `clang`/`lld` (`mingw-w64-ucrt-x86_64-*`).
- **Do not use msys `g++`/`ld`** — they are PE/COFF tools and cannot link the ELF64 kernel. On 2026-11-26 we switched to clang:

```bash
/c/tools/msys64/usr/bin/bash.exe -lc 'export PATH=/ucrt64/bin:$PATH; cd <repo> && \
  make CXX="clang++ --target=x86_64-unknown-none-elf" \
       LD="clang++ --target=x86_64-unknown-none-elf -fuse-ld=lld" iso'
```

- `kernel/runtime/builtins.cc` (added 2026-11-26) provides `memset/memcpy/memmove/memcmp`; clang emits calls to them in freestanding code (e.g. `s = {}`). Keep this file.
- `tools/mkiso.py` must pass relative paths to xorriso (it cannot resolve `C:/...` paths).

## QEMU

QEMU was installed via `choco install qemu` (2026-11-26) → `C:\Program Files\qemu\qemu-system-x86_64.exe`.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -drive format=raw,file=<img> -serial stdio -display none -no-reboot
```

**Pitfall:** QEMU on Windows mangles non-ASCII paths. The repo lives under `...\OneDrive\Документы\` (Cyrillic), so QEMU cannot open images inside the repo tree. Copy `build\hypeai-os.img` to an ASCII path (e.g. `C:\Users\alino\hypeai-os.img`) before booting, or run QEMU from a shell that passes the raw image bytes. Worktrees must also live under ASCII paths (e.g. `C:\Users\alino\hypeaios-wt-*`).

## Boot state (2026-11-26)

Serial log shows `HYPEAI OS BOOT OK`, framebuffer 1024x768x24 EDID-parsed, but several runtime stages report failures (`interrupts=failed`, `vmm=failed`, `kernel threads=failed`, PMM sees no memory) — pre-existing kernel bugs, not build problems.
