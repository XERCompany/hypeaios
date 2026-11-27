# Boot & Type (P8, spec #49)

2026-11-27: seven tickets T1–T7 merged (`devin/hypeai-font-ui` as of `c406ad4` + merges).

## Subsystems
- **Font**: `kernel/font/` — `ttf.{h,cc}` own TrueType parser (cmap fmt4/12, glyf+compound, hmtx, kern fmt0), `ttfrast.{h,cc}` bezier→coverage rasterizer (4 sub-rows, 256KB arena + 768-entry LRU), `hypefont` API (`font_init` inflates `montserrat_ttf_data.cc` — Montserrat.ttf zlib'd 745→294KB via `tools/embed_ttf.py`), `hypetext` coverage blit (draw/wrap/styled/shadow). SDF/genfont.py deleted 2026-11-27 per user direction — native .ttf only.
- **Gfx engine**: `kernel/gfx/hypegfx` — soft shadow (3-pass box blur), inner shadow, lin/radial gradients, elevation 0-4 (dark theme = surface lift +6/level), glow; `gfxbench` PIT-timed.
- **RenderUI**: `kernel/ui/hyperui` — `rui_panel/card/control_bg`; Canvas draw_text routes to TTF (13px default, <10px → bitmap escape hatch); window chrome dots + accent dot; gradient wallpaper.
- **Boot**: `kernel/boot/bootseq` — stage table {name,pct,fn} drives splash; `bootsplash` TTF wordmark 56px, progress bar, PIT spinner; `boot fb=black` clears fb at first C++ frame — no colored garbage before splash.

## Markers
`boot fb=black font=ttf seq=7 splash=shown stage=1..7 done`, `font ttf=ok glyphs=2747 cyr=ok`, `text ttf/wrap/cyr=ok`, `gfx shadow/grad/elev/glow=ok`, `rui text=ttf elev=ok`, `ui polish=ok`, `ui demo=painted widgets=33`
