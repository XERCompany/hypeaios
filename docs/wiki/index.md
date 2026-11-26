# HypeAI OS wiki

Historical record of decisions, environment quirks and subsystem knowledge. Date every claim; cite commits for code claims.

## Environment

- [Build & run on this machine](build-and-run.md) — MSYS2/clang toolchain commands, QEMU invocation, path pitfalls

## Programs

- [HypeAIFont + first UI](hypeai-font-ui.md) — spec #3; also holds program-level notes + index for #13/#23
- [Hello auth + audio](hello-audio.md) — spec #13: sqlite-in-kernel, Hello enroll/auth, HDA driver + PCM playback, login UI
- [CodecApps](codecapps.md) — spec #23: codec registry/arena, image+audio decoders, deferred-format policy

## Research

- `../research/runtime-failures.md` — root cause of dead runtime stages (no .init_array execution)
- `../research/hello-audio.md` — investigation for spec #13 (QEMU devices, HDA/AC97, ALSA-style API)
- `../research/codecapps.md` — investigation for spec #23 (format magic, port-friendly PD/MIT decoder libs)
