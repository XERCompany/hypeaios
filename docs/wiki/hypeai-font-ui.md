# Program: HypeAIFont + first UI

Started 2026-11-26.

- Spec: GitHub issue [#3](https://github.com/XERCompany/hypeaios/issues/3), local copy `.scratch/hypeai-font-ui/spec.md` (as of commit `068d52f`).
- Integration branch: `devin/hypeai-font-ui`.
- Ticket graph: #4 (font metrics+UTF-8), #5 (HPF format+converter), #6 (kernel HPF loader; blocked by #4,#5), #7 (canvas+theme; blocked by #4), #8 (widget framework+layout; blocked by #7), #9 (demo desktop+toasts; blocked by #6,#8).

## Decisions (2026-11-26, user)

- Font format: **custom HPF binary format** (not PSF2/TTF) with `tools/fontc.py` converter from PSF2 sources.
- Font delivery: **embedded into kernel.bin** via generated `.cc` byte array; kernel still parses the blob at runtime (file loading later is a drop-in).
- UI scope: **full first slice** — framework, basic widgets, demo desktop, toast overlays — modeled on `D:\Developers\XER3\src\ui`.
- No WinAPI/Vulkan/STL/heap in ported code; freestanding `hcpp` vocabulary only.

## Progress (2026-11-26)

- All tickets merged to `devin/hypeai-font-ui` (tip `3dd43b6`), issues #4–#9 closed. QEMU serial verified: `fonts=2 default=hypeai-8x16`, `hpf selftest=ok`, `ui demo=painted widgets=7`.
- #4: new `FontGlyph`/`FontFace` metric model, `hcpp/utf8.hpp` decoder, Arial re-expressed.
- #5: `docs/hypeai-font-format.md` HPF v1 spec, `tools/fontc.py` PSF2→HPF converter, vendored `tools/fonts/hypeai-8x16.psfu`.
- #6: `hcpp/fonts/hpf.*` loader (strict validation, binary search, 4 slots), embedded `hypeai-8x16` is default face.
- #7: `kernel/ui/` — geometry, Canvas over Framebuffer, Theme::dark tokens.
- #8: Widget tree (measure/layout/paint), Flex layout, `ui::selftest()`.
- #9: Label/Button/Panel widgets, ToastHost overlays, `ui::demo()` desktop; UTF-8 exercised (Cyrillic renders blank — vendored font is ASCII-only).

## Infra fixes made during the program (2026-11-26)

- `Makefile`: KERNEL_HEADERS glob now covers `.h` (was `.hpp`-only → stale objects).
- Boot layout: `KERNEL_SECTORS` 128→160 (`boot/stage2.asm` + `tools/mkimage.py`); `load_kernel` reads in ≤127-sector int13h chunks (single-shot EDD read silently failed >127).
- Review fixes (`302d566`): test-qemu.sh greps updated to new markers, `Theme::dark()` no longer pins Arial (UI uses loaded HPF), `ui demo=painted` gated on N>0, toast lifetime/dismiss, `resolve_font` dedup, named HPF offset constants.

## Known gaps (supersede or ticket later)

- `FontGlyph::pixel()` bit indexing is correct only for glyph widths ≤8; wider glyphs need the accessor revisited (noted 2026-11-26).
- `docs/hypeaicodepp.md` still documents the pre-#4 8x7 font model.
- Toast lifetime ticks down per `paint()` call — no real timer source wired yet.
- Vendored `hypeai-8x16` covers ASCII-95 only; Cyrillic needs a font with that coverage.

## Program: Hello + Audio (spec #13, 2026-11-26) — COMPLETE

- Ticket graph (all merged & closed): #14 crypto, #15 db, #16 audio-skeleton, #17 hello-core, #18 key+face, #19 HDA, #20 PCM, #21 login-UI, #22 decoders. Depended on #12 (static init, `dd68202`) — all 7 runtime stages run (`stages=7 completed=7 failed=0`).
- Details: see [hello-audio.md](hello-audio.md).
- **Superseded:** the 2026-11-26 plan above said credentials DB would be a native `hypedb` engine, NOT vendored sqlite3 — written while the #15 port was mid-flight. What actually merged (commit `2fb6fd8`+): **vendored SQLite 3.50.4 on a native "hypevfs" memdb VFS** + heap over PMM (`sqlite=ok`). A pure from-scratch DB remains an open option (see [hello-audio.md](hello-audio.md) open questions).

## Program: UI components v2 (spec #32, 2026-11-27) — COMPLETE

- All tickets #33–#40 merged; research source `docs/research/ui-registries.md`.
- As of `3bc3842`: Canvas v2 (line/alpha/rrect/circle), Theme v2 (20 tweakcn tokens, dark+light, `hard_shadow` flag), FocusManager, 20+ widgets incl. InputOTP/Tabs/Dialog/charts/Spinner/Marquee, AnimTicker driven by `pit_ticks()`.
- **Critical fix landed inside #38** (`6d2bef7`+): kernel `.bss` was never zeroed AND PMM only reserved 2MB — large-model sections `.ltext/.lrodata/.ldata/.lbss` are now collected in `kernel/linker.ld`, `main.cc` memsets bss before `.init_array`, `pmm.cc` reserves through `__bss_end`. Without this, static objects in new code got garbage vptrs → #GP and heap corruption.

## Program: CodecApps (spec #23, 2026-11-26) — COMPLETE

- Ticket graph (all merged & closed): #24 core registry/magic/arena → #25 BMP/PNM/TGA/QOI, #26 PNG+inflate, #27 JPEG baseline, #28 GIF/ICO/EXIF/TIFF, #29 ADPCM/G.711/MP3/FLAC → #30 Ogg/Vorbis/ALAC → #31 integration + demo image.
- Details: see [codecapps.md](codecapps.md).
