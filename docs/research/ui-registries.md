# UI registries research — shadcn ecosystem → kernel/ui port spec

Scope: mine the shadcn registry directory for component/pattern specs portable to the
immediate-paint Canvas (`fill_rect`/`stroke_rect`/`draw_text`/`set_clip`,
kernel/ui/canvas.h:22-28). No DOM/JS: retained `Widget` tree (max 16 children,
kernel/ui/widget.h:11) + `Flex` + `Theme` tokens already exist; input is char-routed
(flowlogin.h:14) — no pointer yet.

Verified 2026-11-26: `directory.json` lists **613** namespaced registries.

## Registry inventory

| Site | URL | Offers | Verdict |
|---|---|---|---|
| shadcn/ui official | ui.shadcn.com | 62 primitives, 28 blocks, 70 chart variants | PRIMARY spec source |
| RetroUI (neobrutalism) | retroui.dev | 54 neobrutalist variants of shadcn set | HIGH — flat fill + thick border + offset shadow = zero-dep look |
| 8bitcn | 8bitcn.com | 8-bit pixel components + game blocks (KPI dash, terminal shell, 404s) | HIGH — aesthetic twin of a framebuffer hobby OS |
| Magic UI | magicui.design | ~90 animated comps: marquee, number-ticker, typing, ripple, grid/dot/striped patterns, dither | MED — effects port as tick-driven painters |
| DiceUI | diceui.com | ~40 behavior-rich primitives: sortable, stepper, tags-input, listbox, mask-input, masonry, gauge, rating | HIGH — interaction specs, no DOM needed |
| Kibo UI | kibo-ui.com | gantt, kanban, calendar, tree, color-picker, QR, code-block, dropzone, pill | MED-HIGH — structural specs |
| tweakcn | tweakcn.com | theme token editor: primary/secondary/accent/destructive/muted/card/popover/border/ring/chart1-5/sidebar | HIGH — Theme v2 taxonomy |
| Origin UI | originui.com | ~400 input/form variant specs | MED — input-variant reference |
| Intent UI / Basecn | intentui.com | radix/base-ui-style primitives | MED — focus/keyboard behavior specs |
| Aceternity | ui.aceternity.com | marketing animated comps | LOW — few survivors of a no-DOM port |
| animate-ui / motion-primitives | animate-ui.com | easing/motion primitives | LOW-MED — easing table only |
| assistant-ui / prompt-kit / ai-elements | assistant-ui.com | AI chat primitives | MED — chat surface fits "HypeAI" branding |
| lytenyte | 1771technologies.com | full data grid | LOW — too heavy for 16-child Widget |
| registry.directory | registry.directory | meta-index, 122 registries / ~27k items | reference only |

## Component candidates

Ranked. "Trivial" = rects+text only; "medium" = focus/tick state; "hard" = pointer or
complex layout. Most need 3 new Canvas primitives first (see UI2-01).

1. **Alert** (shadcn) — bordered strip, tone border/icon slot, title+desc. Trivial. Inline errors in flowlogin vs silent fail.
2. **Progress** — track rect + % fill + label. Trivial. Boot/init/buffer status.
3. **Badge** — small pill/rect label; solid/outline/destructive/secondary. Trivial. Status chips.
4. **Card** — Panel minus chrome: header/title/desc/content/footer slots. Trivial. Composes everything.
5. **Separator** — 1-2px h/v rule. Trivial. Menus, toolbars, forms.
6. **Kbd** — bordered keycap box + glyph, grouped. Trivial. Shortcut help; suits keyboard-driven shell.
7. **Skeleton** — pulsing placeholder rects (tick-modulated shade). Trivial-Med. Codec/fs loading.
8. **Spinner** — 8-phase rotor via draw_line or rotating glyph. Medium. Busy indicator.
9. **Empty** (new shadcn) — centered glyph+title+desc+CTA. Trivial. Empty dirs/states.
10. **Item** (new shadcn) — icon + title/desc + trailing-action row. Trivial. Settings/file rows.
11. **Field** — label+control+desc/error wrapper reusing Alert tones. Trivial-Med. flowlogin polish.
12. **Tabs** — tab strip (accent underline/fill) + content swap, L/R nav. Medium. Settings/demo shell.
13. **Checkbox + Switch + RadioGroup** — box/toggle/ring glyphs, Space toggles, focus ring. Medium. Settings.
14. **Input OTP** — N boxed cells, per-cell caret, auto-advance. Medium. PIN mode for flowlogin.
15. **Stepper** (diceui) — numbered circles + rule + labels; done/current/todo. Trivial-Med. Setup wizard, boot stages.
16. **Tags Input** (diceui/kibo) — chips inside TextInput; Enter commits, BS pops. Medium. Filters/commands.
17. **Listbox/Select** — option list, up/down nav, Enter select, type-jump. Medium-Hard (popup host). Any picker.
18. **Accordion/Collapsible** — header + expand region + chevron. Medium. Log/settings viewer.
19. **Table lite** — header + rows, fixed col widths, clip+ellipsis, row select. Medium. fs/task viewers.
20. **File Tree** (kibo/magicui) — indented rows, expand glyphs, selection. Medium. Future VFS browser.
21. **Bar/Line chart lite** (shadcn chart) — axis rule, gridlines, bars=fill_rect, line=Bresenham, chart[5] tokens. Medium. Kernel monitor HUD.
22. **Marquee** — scrolling strip, tick offset, edge fade via clip. Trivial-Med. Boot ticker/flourish.
23. **Number Ticker** — counting label on tick. Trivial. KPI counters.
24. **Dialog/AlertDialog** — modal card over alpha-dim scrim, focus trap, Esc/Enter. Medium (generalize ToastHost → OverlayHost). Confirm prompts.
25. **Hard-shadow mode** (RetroUI/8bitcn) — offset dark rect + thick border; Theme flag. Trivial. Distinct OS personality, ~2 extra rects/widget.

Defer/hard: Command palette (fuzzy), Calendar (RTC grid), sorting DataTable, Resizable,
Sidebar shell, Kanban/Gantt drag, Tooltip/HoverCard/ContextMenu (pointer), Carousel,
ScrollArea (needs scroll model), Combobox (phase 3).

## Recommended next program — "UI components v2"

| Ticket | Contents | Serial acceptance |
|---|---|---|
| UI2-01 Canvas v2 | draw_line (Bresenham), fill_rect_alpha, fill_rounded_rect, glyph-circle/square; extend selftest pixel asserts | `ui2 primitives ok lines=N rrect=1 alpha=1` |
| UI2-02 Theme v2 | tweakcn-style tokens: primary/secondary/destructive/success/warning/muted_fg/ring/chart[5]; radius honored; light+dark statics | `ui2 theme tokens=N variants=2` |
| UI2-03 Feedback kit | Alert(4 tones), Progress, Badge(4), Separator, Skeleton | `ui2 feedback ok alert=4 badge=4` |
| UI2-04 Focus+selection | FocusManager (tab order, ring token), Checkbox, Switch, RadioGroup, Tabs | `ui2 focus order=N tabs=sel` |
| UI2-05 Overlay v2 | OverlayHost w/ dim scrim, Dialog, AlertDialog, Esc/Enter; ToastHost under host | `ui2 overlay open=1 close=1` |
| UI2-06 Entry widgets | Field, InputOTP(n cells), TagsInput, Listbox (up/down/Enter/jump) | `ui2 entry otp=6 tags=3 list=sel` |
| UI2-07 Data+viz | Table lite, Item, Empty, Stepper, Bar+Line chart w/ chart tokens, NumberTicker | `ui2 data table=r3 chart bars=5` |
| UI2-08 Anim ticker | tick repaints w/ dirty flag: Spinner, Marquee, Skeleton pulse; hard-shadow flag | `ui2 anim tick=F spin=1 shadow=1` |

Test pattern: per-widget selftest stanza asserting layout bounds + framebuffer pixels
(kernel/ui/selftest.cc); serial markers via kernel/serial.hpp as in demo.cc.

## Codebase grounding (as of 345add0)

- `Canvas` has only `draw_pixel`, `fill_rect`, `stroke_rect`, `draw_text`, clip — no line/circle/alpha/round-rect. UI2-01 is prerequisite for ~half the candidates.
- `Widget` retained-mode, max 16 children, no event dispatch; input manual (TextInput::push_char). FocusManager needed before interactive widgets.
- `Theme` 7 colors + 4 spacings + 3 radii — radii declared but unusable without rounded-rect painter. tweakcn taxonomy maps onto v2.
- `ToastHost` (max 8, timed) is the only overlay — Dialog needs OverlayHost generalization.
