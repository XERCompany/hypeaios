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
