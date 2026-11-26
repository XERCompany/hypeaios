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
