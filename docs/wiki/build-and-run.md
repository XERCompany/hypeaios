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
