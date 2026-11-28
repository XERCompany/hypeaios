# HypeUI design-system spec — tokens, widget anatomy, paint mapping

Scope: the durable visual/interaction language for HypeAI OS userland, rendered by
the immediate-mode `Canvas` (`fill_rect`, `fill_rounded_rect`, `stroke_rounded_rect`,
`fill_circle`, `draw_text`, `set_clip`, `fill_rect_alpha` — `kernel/ui/canvas.h:18-40`)
on a single linear framebuffer with rect-based damage. No DOM, no CSS, no per-layer
surfaces: every widget is `measure → layout → paint` over a `Rect`
(`kernel/ui/widget.h:11-36`), children capped at `Widget::max_children = 16`
(`kernel/ui/widget.h:11`), animation driven by `AnimTicker` frames
(`kernel/ui/anim.h:8-19`), state changes notify the parent and mark bounds dirty.

Vocabulary is ported from the shadcn/ui component catalog (anatomy, variant names,
size ladders, state machines) but ALL naming here is "HypeUI" — widget classes,
tokens and slots use the names in this document only. Visual direction follows the
user's reference style: clean modern dashboards, flat card surfaces, soft
multi-stop shadows carrying all elevation, hairline borders, one restrained accent
hue.

Sources consulted (2026-12):
- shadcn/ui component docs — anatomy & composition for button, card, dialog,
  dropdown-menu, tabs, select, input, switch, slider, toast, tooltip, badge,
  avatar, progress, scroll-area, separator (URLs in §9).
- shadcn/ui theming doc — semantic surface/foreground token-pair convention.
- Tailwind default shadow/radius/spacing scales — concrete dy/blur/alpha values
  used as the starting point for the HypeUI shadow ramp.
- Radix UI / Base UI primitive anatomy — part decomposition for composite
  widgets (slider track/range/thumb, select trigger/content/item, etc.).

---

## 1. Design principles

1. **Flat surfaces, shadows carry elevation.** Fill colors distinguish *kind*
   (background vs surface vs overlay); they never simulate depth by themselves.
   A card on `bg` is the same `surface` white as everything else — elevation is
   expressed ONLY by the shadow ramp (§2.3) plus a 1px hairline border. Removing
   all shadows must still leave a legible UI (borders + text contrast), and
   removing all borders must still show layering (shadows). Neither is optional.
2. **Hairline borders everywhere.** 1px `border` strokes on every bounded
   surface: cards, inputs, menus, dialogs, separators are 1px lines, never 2px
   fills. `border-strong` exists for inputs and emphasis only. No gradients on
   chrome; gradients are reserved for accent fills and chart data.
3. **Three radii only.** `r6` for interactive controls, `r8` for bounded surfaces,
   `r12` for overlays; `r4`/`r3` for tiny inline elements (badge, checkbox, kbd)
   and `full` for pills/avatars. Squarer than r4 reads as a bug; rounder than r12
   reads as a toy.
4. **One restrained accent.** Exactly one accent hue (indigo) signals
   interactivity: focus rings, selected states, primary-action fill, links,
   progress fill, checked marks. It never appears on large background areas and
   never on chrome borders at rest. Everything else is neutral gray.
   `danger`/`success`/`warn` are semantic, not decorative.
5. **Density over padding.** Controls are 28-36px tall, cards pad 16-24px,
   text is 13px UI default. Whitespace is created by spacing tokens, not by
   oversized controls.
6. **States are painted, not implied.** Every interactive widget paints
   normal/hover/pressed/focused/disabled explicitly (§5). Focus is ALWAYS a
   painted ring, never inferred from caret position.
7. **Immediate-mode honest.** A widget draws exactly its own bounds (minus what
   its clip rect removes), marks damage on every state transition, and keeps
   per-frame paint cost proportional to visible rect area — no full-surface
   clears, no retained layer buffers.

---

## 2. Design tokens

All widget paint code reads tokens, never literal colors. Tokens live in
`Theme` (`kernel/ui/theme.h:8-38`); the tables below are the target `Theme v2`
field set. Colors are `u32` 0xRRGGBB; alphas are `u8` 0-255 for
`fill_rect_alpha`.

### 2.1 Color tokens — light palette (default)

| Token | Value | Usage |
| --- | --- | --- |
| `bg` | `#FAFAFA` | window/desktop background, page shell |
| `surface` | `#FFFFFF` | cards, panels, inputs at rest |
| `surface-sunk` | `#F4F4F5` | inset areas: code blocks, wells, table zebra |
| `elev` | `#FFFFFF` | floating surfaces (menus, popovers, tooltips, dialogs) — same fill as surface, distinguished by shadow-lg/xl + border |
| `border` | `#E4E4E7` | hairlines: card edges, separators, table rules |
| `border-strong` | `#D4D4D8` | input borders, hover-state edges |
| `text` | `#18181B` | primary text, icons |
| `text-dim` | `#71717A` | descriptions, placeholders, captions, disabled text |
| `accent` | `#6366F1` | focus ring, selected fill, links, primary action |
| `accent-fg` | `#FFFFFF` | text/icons on `accent` fills |
| `accent-soft` | `#EEF2FF` | hover/selected wash on lists, menu items, tabs |
| `danger` | `#DC2626` | destructive actions, invalid borders/text |
| `danger-soft` | `#FEF2F2` | destructive hover wash, alert fill |
| `success` | `#16A34A` | confirmations, progress-ok, badges |
| `warn` | `#D97706` | warnings, attention states |
| `track` | `#E4E4E7` | slider/progress/switch unfilled track |
| `ring` | `#6366F1` @ 40% | focus ring (2px) |
| `scrim` | `#000000` @ 40% | dialog/sheet backdrop dim |
| `shadow` | `#09090B` | shadow source color (alpha from §2.3) |

### 2.2 Color tokens — dark palette

| Token | Value | Notes |
| --- | --- | --- |
| `bg` | `#09090B` | near-black, not pure black |
| `surface` | `#131316` | cards/inputs; ΔL from bg ≈ 4% only |
| `surface-sunk` | `#0D0D10` | darker than bg: sunk wells |
| `elev` | `#1C1C20` | overlays get a real fill lift (dark shadows read poorly; fill does more work) |
| `border` | `#27272A` | |
| `border-strong` | `#3F3F46` | |
| `text` | `#FAFAFA` | |
| `text-dim` | `#A1A1AA` | |
| `accent` | `#818CF8` | accent lifts ~2 stops in dark mode |
| `accent-fg` | `#0A0A0C` | |
| `accent-soft` | `#6366F1` @ 15% | wash, not a solid |
| `danger` | `#EF4444` | |
| `danger-soft` | `#EF4444` @ 12% | |
| `success` | `#22C55E` | |
| `warn` | `#F59E0B` | |
| `track` | `#27272A` | |
| `ring` | `#818CF8` @ 45% | |
| `scrim` | `#000000` @ 55% | stronger dim needed on dark bg |
| `shadow` | `#000000` | dark shadows use higher alphas (×1.6 the §2.3 values, cap 0.60) |

Pairing rule (from the shadcn token convention): every surface token implies a
foreground token for content drawn on it — `accent`→`accent-fg`,
`danger`→`text` on `danger-soft` / `accent-fg` on solid `danger`, `elev`→`text`.
Widgets take the foreground from the pair, never free-pick `text` on a tinted
fill.

### 2.3 Shadow scale (elevation ramp)

Soft multi-stop shadows. Each step = 1-2 rounded-rect fills behind the widget,
dilated and offset. `dy` = vertical offset, `blur` = spread radius of the
soft-edge approximation, `a` = alpha of `shadow` color. On hardware without
true blur, approximate each stop as N concentric dilated rounded rects with
linearly decreasing alpha (N = blur, ~1 alpha step per px — see §5).

| Level | Stops (dy, blur, alpha) | Use |
| --- | --- | --- |
| `shadow-none` | — | inline elements, sunk wells |
| `shadow-xs` | (1, 2, 0.05) | buttons at rest, inputs, badges on color |
| `shadow-sm` | (1, 3, 0.08) | cards at rest, small panels |
| `shadow-md` | (2, 6, 0.10) + (1, 2, 0.06) | raised cards, sticky headers, dropdown triggers |
| `shadow-lg` | (8, 16, 0.10) + (3, 6, 0.06) | menus, popovers, tooltips, selects-open |
| `shadow-xl` | (16, 32, 0.12) + (6, 12, 0.08) | dialogs, sheets, command palette |
| `shadow-2xl` | (24, 48, 0.20) | window-level modal, drag-lifted card |

Elevation law: rest state never above `sm`; overlays never below `lg`; pressing
a widget drops it one level (press = flatten); hovering raises buttons/rows
from `xs`→`sm` only (menus/cards do NOT float on hover — dashboards stay flat).

### 2.4 Radius scale

| Token | px | Use |
| --- | --- | --- |
| `r3` | 3 | kbd, tiny inline chips |
| `r4` | 4 | badge, checkbox, tag, scroll thumb |
| `r6` | 6 | buttons, inputs, selects, switches(track), tabs list, menu items |
| `r8` | 8 | cards, panels, tables, tooltip, dropdown content |
| `r12` | 12 | dialogs, sheets, drawers, popover content |
| `r-full` | h/2 | pills: avatar, badge-pill, switch knob, progress caps |

### 2.5 Spacing scale (4px base)

| Token | px | Use |
| --- | --- | --- |
| `s0` | 0 | — |
| `s1` | 4 | icon↔label gap, badge padding-x pair |
| `s2` | 8 | control inner padding-y, label↔control gap, menu item padding-y |
| `s3` | 12 | control padding-x, card-sm padding, section gap inside card |
| `s4` | 16 | card padding (default), dialog section gap |
| `s5` | 20 | card header gap, form row gap |
| `s6` | 24 | dialog padding, large section breaks |
| `s8` | 32 | page section spacing |
| `s12` | 48 | modal vertical margins |
| `s16` | 64 | page gutters |

Insets are written `pad(x,y)` in token units, e.g. button-md = `pad(s3,s2)`.

### 2.6 Typography scale

Single family (kernel bitmap/vector face resolved by `Theme::resolved_font()`).
Sizes are pixel heights; weights map to the face's available cuts (synthesize
bold by 1px double-strike only as last resort).

| Token | px | Weight | Use |
| --- | --- | --- | --- |
| `t-xs` | 11 | 500 | badge text, kbd, table caption, toast meta |
| `t-sm` | 12 | 400/500 | descriptions, placeholders, tab labels, menu shortcuts |
| `t-base` | 13 | 400/500 | UI default: buttons, inputs, menu items, body |
| `t-md` | 14 | 400/500 | emphasized body, card description |
| `t-lg` | 16 | 600 | card title, dialog title |
| `t-xl` | 20 | 600 | section heading, sheet title |
| `t-2xl` | 24 | 600 | page heading |
| `t-num` | 32 | 600 | KPI numbers (tabular figures) |

Line height 1.4×px for single-line controls (baseline = rect_center +
descent/2); 1.5× for wrapped description text. Disabled text = `text-dim`
(at full alpha — dimming is by color choice, not alpha stacking).

### 2.7 Z-order / elevation layers

Paint order bottom→top inside the desktop pass:

| Layer | Contents | Shadow |
| --- | --- | --- |
| L0 | `bg` fill, wallpaper | — |
| L1 | window/card surfaces | xs-md |
| L2 | in-surface chrome (taskbar, toolbars) | sm |
| L3 | dropdown menus, selects, popovers | lg |
| L4 | tooltips, toasts | lg |
| L5 | dialogs/sheets + `scrim` over L0-L4 | xl |
| L6 | drag ghost, boot/splash | 2xl |

### 2.8 Interaction timing (frame-based)

At ~60fps via `AnimTicker` (4 ticks/frame, `kernel/ui/anim.h:11`):

| Motion | Frames | Curve |
| --- | --- | --- |
| hover fade in/out | 6 / 9 | linear alpha lerp |
| press darken | 0 (instant) | — |
| focus ring | 0 (instant) | — |
| menu/popover open | 8 | fade + 4px rise, ease-out |
| dialog open | 9 | scrim fade + 2% scale-in |
| toast enter/exit | 10 / 8 | slide 16px + fade |
| progress indeterminate | 90 loop | linear sweep |
| switch knob travel | 6 | ease-out x-lerp |

---

## 3. Widget catalog

Anatomy uses `Widget > slot` notation: a slot is a named sub-rect the widget
paints/hit-tests (not necessarily a child `Widget`). "Keys" lists the input
contract (`handle_key(char)`, `kernel/ui/widget.h:16`). All widgets honor the
shared state machine in §5.

### 3.1 `Button`

- **Anatomy:** `Button > icon? + label + icon?` (inline-start / inline-end
  icon slots, `s1` gap).
- **Variants:** `primary` (accent fill, accent-fg text, shadow-xs);
  `secondary` (surface-sunk fill, text, border); `outline` (surface fill,
  border-strong hairline, shadow-xs); `ghost` (transparent; hover → accent-soft
  wash); `destructive` (danger fill, accent-fg); `link` (no box, accent text,
  underline on hover).
- **Sizes:** `xs` h24 `pad(s2,s1)` t-xs · `sm` h28 `pad(s3,s2)` · `md` h36
  `pad(s3,s2)` t-base · `lg` h40 `pad(s4,s2)` · `icon` 36×36, `icon-sm` 28×28,
  `icon-xs` 24×24 (square, single centered icon).
- **States:** normal / hover (fill +8% toward text for fills, accent-soft wash
  for ghost/outline) / pressed (shadow-none, fill +4% dark, translate-y 0 —
  never move the label) / focused (ring) / disabled (surface-sunk fill,
  text-dim, no shadow) / loading (spinner slot replaces label, same rect).
- **Keys:** Enter/Space activate while focused. Activation fires on release.

### 3.2 `IconButton`

- Button variant `size=icon*` promoted to its own widget: `IconButton > icon`.
  Variants: `ghost` (default — taskbar, card actions), `outline`, `primary`.
  Same state machine as Button. Square r6 hit-rect ≥24px even when the glyph
  is 16px.

### 3.3 `Card`

- **Anatomy:** `Card > CardHeader{ CardTitle, CardDescription, CardAction } +
  CardContent + CardFooter`.
- **Paint:** surface fill, r8, border hairline, shadow-sm. Static — cards do
  NOT hover-lift (dashboard flatness rule).
- **Slots:** header = title (t-lg) + optional description (t-sm text-dim) +
  `CardAction` top-right (IconButton/badge); content padding `s4` all sides,
  section gap `s4`; footer = action row, top hairline optional.
- **Sizes:** `sm` (`pad s3`, gap `s3`), `default` (`pad s4`, gap `s4`).
- **States:** normal only (non-interactive); `data-invalid` variant paints
  danger-soft border. Interactive cards are `Card` + Button hit-rect, not a
  card state.

### 3.4 `TextInput`

- **Anatomy:** `TextInput > icon-start? + text-slot + icon-end? + caret`.
- **Paint:** surface fill, r6, border-strong hairline, shadow-xs; text-slot
  clipped (set_clip) for horizontal scroll when content > width.
- **Sizes:** `sm` h28, `md` h36, `lg` h40; inner `pad(s3,0)`.
- **States:** normal / hover (border→`border-strong`→`text-dim`) / focused
  (border→accent 1px + ring at 40%) / disabled (surface-sunk, text-dim) /
  `invalid` (danger border + danger ring) / placeholder (text-dim) /
  selection (accent @ 25% band under selected glyphs).
- **Keys:** full ASCII edit contract — backspace, delete, arrows, Home/End,
  Ctrl+A/C/X/V via clipboard (`kernel/ui/clipboard.h`). Caret blinks 30-frame
  period via AnimTicker.

### 3.5 `Label` + `Field`

- `Label > text` — t-sm 500, text; optional `(required)` accent asterisk.
- `Field > Label + control + hint? + error?` — vertical stack, `s2` gaps;
  `error` line is t-xs danger and swaps control to `invalid` state. Field is
  the FORM unit; never ship a bare input without Label association.

### 3.6 `Checkbox`

- **Anatomy:** `Checkbox > box(16×16) + label?`.
- **Paint:** box r4, border-strong hairline; checked → accent fill + accent-fg
  check glyph (2px stroke polyline); `indeterminate` → dash glyph.
- **States:** unchecked/checked/indeterminate × hover (border→text-dim) /
  pressed / focused (ring) / disabled.
- **Keys:** Space toggles.

### 3.7 `RadioGroup`

- **Anatomy:** `RadioGroup > RadioItem{ circle(16) + label } × n`, vertical
  `s3` spacing (horizontal variant `s4`).
- **Paint:** circle stroke border-strong 1.5px; selected → accent stroke +
  5px accent inner dot. Never a fill — a ring + dot.
- **States:** per-item hover/focused/disabled; group owns single focus —
  arrows move selection AND focus together.
- **Keys:** Up/Down (or Left/Right) cycle; Tab enters/leaves group as one
  stop.

### 3.8 `Switch`

- **Anatomy:** `Switch > track(36×20, r-full) + knob(16) + label?`.
- **Paint:** off → track fill `track`; on → accent fill. Knob: surface circle,
  shadow-xs, travels 16px horizontally over 6 frames.
- **States:** off/on × hover (track +4% toward text) / pressed (knob stretches
  to 18×16 — squash, then settle) / focused (ring on track) / disabled
  (track→surface-sunk, knob→border).
- **Keys:** Space/Enter toggle. Fires `on_change` on commit, not during travel.

### 3.9 `Slider`

- **Anatomy:** `Slider > track(h4, r-full, full width) + range(accent fill,
  0→value) + thumb(16 circle)`. Optional `ticks` (1px dots under track) and
  `value-label` above thumb.
- **Paint:** track `track` color; range `accent`; thumb surface fill,
  border-strong, shadow-xs → shadow-sm while grabbed.
- **States:** normal / hover (thumb shadow-sm) / grabbed (thumb +1 level
  shadow, track stays flat) / focused (ring on thumb) / disabled / `invalid`.
- **Keys:** Left/Right ±step, Home/End min/max, PageUp/Down ±10·step. Value
  changes notify parent each step; grab-release fires commit.

### 3.10 `Select`

- **Anatomy:** `Select > SelectTrigger{ icon?, value-slot, chevron } +
  SelectContent{ listbox, scroll-buttons } > SelectItem{ label + check-slot }`.
- **Trigger:** identical paint contract to `Button outline` + 16px chevron
  (rotates 180° open); open state keeps border accent.
- **Content:** `elev` fill, r8, border, shadow-lg; item h32 `pad(s3,s2)`,
  selected → accent-soft fill + 16px check glyph in check-slot (icon column
  reserved on ALL items); type-ahead highlight ring inside.
- **States:** closed / open (content at L3, anchored below trigger, flips up
  on edge clip) / item hover (accent-soft) / disabled.
- **Keys:** Enter/Space/Down opens; Up/Down moves highlight; Enter commits,
  Esc cancels (revert highlight, keep old value); type-ahead jump.

### 3.11 `Tabs`

- **Anatomy:** `Tabs > TabsList{ TabsTrigger × n } + TabsContent`.
- **Paint:** list = surface-sunk fill r6 `pad(s1)`; trigger = transparent r6
  h28 `pad(s3,s2)` t-sm; active trigger → surface fill + shadow-xs + text
  (inactive = text-dim). This is the "segmented" style; a `line` variant
  replaces the pill with a 2px accent underline under the active trigger on a
  hairline-bottomed list.
- **States:** per-trigger hover (text-dim→text) / active / focused / disabled.
  Content panel swaps instantly (no slide — immediate-mode honest).
- **Keys:** Left/Right cycles triggers while list focused; Tab moves into
  panel.

### 3.12 `Dialog` (+`AlertDialog`)

- **Anatomy:** `Dialog > scrim + DialogContent{ DialogHeader{ DialogTitle,
  DialogDescription, close-IconButton }, body, DialogFooter{ buttons } }`.
- **Paint:** scrim = `scrim` alpha over everything ≤L4 (L5); content = `elev`
  (light: surface) r12, border, shadow-xl, `pad(s6)`, max-w ~480px, centered.
  Footer buttons right-aligned `pad(0,s2)` gap `s2`.
- **States:** closed / open (9-frame scrim fade + 2% scale-in) /
  `modal` — while open, L≤4 receives NO input and widgets under scrim do not
  repaint hover states. AlertDialog variant: same anatomy, footer buttons are
  `destructive` primary + `outline` cancel, Esc disabled (explicit choice
  required).
- **Keys:** Esc closes (Dialog only); Tab cycles INSIDE dialog only (focus
  trap); Enter activates focused button.

### 3.13 `Toast`

- **Anatomy:** `Toast > icon-slot + title + description? + action-Button? +
  close-IconButton` in a stacked overlay region (corner-anchored, `s3` gap,
  max 5 visible).
- **Paint:** `elev` r8 border shadow-lg w ~360px; left edge carries a 3px
  semantic stripe (accent/danger/success/warn) OR a 16px status glyph — pick
  one per toast, not both.
- **Variants:** `default`, `success`, `danger`, `warn`; `action` adds an
  outline Button slot.
- **States:** entering (slide+fade 10f) / visible / exiting (8f) /
  auto-dismiss timer paused on hover.
- **Keys:** toast region is focusable; Esc dismisses focused toast.

### 3.14 `Tooltip`

- **Anatomy:** `Tooltip > label-text` anchored to a target rect.
- **Paint:** `text` fill (inverted!) r8? — no: r6, `pad(s2,s1)` t-xs, fg =
  `surface`, shadow-lg, 4px offset from target, arrow omitted (keep flat).
- **States:** hidden / delay-armed (≥30 frames hover) / visible (fade 6f).
  Never interactive; disappears on any input or hover-leave.

### 3.15 `DropdownMenu` (+ ContextMenu)

- **Anatomy:** `DropdownMenu > trigger + MenuContent{ MenuLabel?, MenuItem × n,
  MenuSeparator, MenuCheckboxItem, MenuRadioGroup{ MenuRadioItem }, Sub{
  SubTrigger + SubContent } }`.
- **Item anatomy:** `item > check/icon-slot(16) + label + shortcut-slot +
  submenu-chevron?` h28 `pad(s2+s3, s2)`.
- **Paint:** content `elev` r8 border shadow-lg min-w 160px at L3; separator =
  1px `border` inset `s1`; label = t-xs text-dim `pad(s3,s1)`; shortcut =
  t-xs text-dim right-aligned; destructive item label = `danger`, hover wash =
  `danger-soft`.
- **States:** item hover/highlight (accent-soft fill; checked items keep their
  glyph — wash goes under it) / disabled (text-dim, no wash) / submenu open
  (SubContent at L3+1, overlaps parent by 4px).
- **Keys:** Up/Down move, Right/Left enter/leave submenu, Enter activate,
  Esc close-all, type-ahead jump, shortcuts active while open.

### 3.16 `Badge`

- **Anatomy:** `Badge > icon? + text`, pill or r4 `pad(s2,s1)` t-xs 500.
- **Variants:** `primary` (accent fill/accent-fg), `secondary` (surface-sunk,
  text), `outline` (transparent + border), `danger` (danger fill/accent-fg),
  `success`, `warn`. `dot` variant = 6px status circle, no text.
- Non-interactive; if clickable it's a Button, not a Badge.

### 3.17 `Avatar`

- **Anatomy:** `Avatar > image-slot | initials-fallback + status-dot?`.
- **Paint:** r-full clip; fallback = surface-sunk fill + t-sm text-dim
  initials; optional 8px status dot (success/warn/danger) with 2px surface
  ring. Sizes: 24 / 32 / 40 / 48 (`sm/md/lg/xl`).
- `AvatarGroup` = overlapped row, each shifted −8px, surface 2px ring between.

### 3.18 `Progress`

- **Anatomy:** `Progress > track(h8, r-full) + fill(accent, r-full, width =
  value%)` + optional `label` right of track.
- **States:** determinate 0-100 / `indeterminate` (32px accent segment sweeps,
  90-frame loop) / `success` (fill→success at 100%) / `danger` (fill→danger
  when flagged). Never animates layout — fill width only, damage = track rect.
- Thin variant h4 for inline/file-op use; buffer variant adds a second
  lighter range under the fill (accent @ 30%).

### 3.19 `ScrollArea`

- **Anatomy:** `ScrollArea > viewport(clip rect) + v-scrollbar + h-scrollbar +
  corner`. Scrollbar = 8px track (transparent) + thumb (r4, `border-strong` @
  60% → `text-dim` on hover/drag).
- **Paint:** viewport = set_clip on content bounds; children paint through it.
  Scrollbars auto-hide (fade out 20f after last scroll, show on scroll/hover
  of edge 12px) — the "overlay" mode. `always` mode reserves the 8px.
- **Keys:** Up/Down ±line, PageUp/Down ±viewport−`s4`, Home/End; focus lives
  on the area, not the thumb.

### 3.20 `Separator`

- **Anatomy:** `Separator` — 1px `border` line, horizontal (w=100%, h=1) or
  vertical (h=100%, w=1); `pad` variant insets `s3` on both ends.
- Semantic only — menus and toolbars group with it; decorative spacing uses
  spacing tokens, NOT separators.

### 3.21 `Alert`

- **Anatomy:** `Alert > icon-slot + title + description + action?`, r8 `pad(s4)`
  border.
- **Variants:** `default` (surface, border, text-dim icon), `danger`
  (danger-soft fill, danger border+icon), `warn`, `success`. Inline, not
  floating — a static card, NOT a toast.

### 3.22 `Skeleton` / `Spinner`

- `Skeleton > rect(r4, surface-sunk) ` shimmer = 24px-wide `accent-soft` band
  sweeping L→R every 90 frames, painted INSIDE clip. Block-shaped loading
  placeholders matching final layout rects.
- `Spinner > arc` = 270° accent arc on 16/20/24px circle, rotating 30°/4f.
  Used inside buttons/loading states; Skeleton for content, Spinner for
  actions.

---

## 4. Immediate-mode paint mapping

Shared paint vocabulary — every widget decomposes into these ops:

| Paint need | Canvas op | Notes |
| --- | --- | --- |
| flat surface / control fill | `fill_rounded_rect(r, token)` | one op, no gradient |
| hairline edge | `stroke_rounded_rect(r-1 inset, border)` | stroke INSIDE bounds |
| focus ring | `stroke_rounded_rect(r+2 outset, ring)` | painted OUTSIDE bounds → bounds must include 3px ring margin or ring marks extra damage |
| shadow stop | `fill_rounded_rect(r+spread, shadow @ α)` offset by dy | painted BEFORE the surface rect, clipped away from widget itself |
| hover wash | `fill_rounded_rect(r, accent @ 12%)` over item rect | order: wash → glyphs |
| checked/selected fill | `fill_rounded_rect` accent or accent-soft | |
| track/fill bars | `fill_rounded_rect(r-full)` × 2 | fill width lerped, damage = whole track |
| text | `draw_text(baseline, token)` | baseline = rect.cy + descent/2 |
| glyph/icon | `draw_text` icon-font glyph OR polyline fills | 16px icon box, 1.5px stroke weight equivalent |
| caret/selection | `fill_rect` / `fill_rect_alpha` | inside input clip |
| clip for scroll | `set_clip(viewport)` … `reset_clip()` | children paint through |
| scrim | `fill_rect_alpha(full-screen, scrim)` | L5 only |
| shadow approx (no blur) | blur px → N dilated r-rects, α from `a·(1 - i/N)` | N=blur; cost ∝ blur — cap effective blur ≤16, larger steps dilate in 2px strides |

### 4.1 Per-widget paint recipe

| Widget | Op sequence (back→front) |
| --- | --- |
| Button | shadow → fill_rounded(fill) → stroke(border var) → icon → label → ring? |
| Card | shadow-sm → fill_rounded(surface,r8) → stroke(border) → header slots → divider? → content → footer |
| TextInput | shadow-xs → fill_rounded(surface,r6) → selection band → text/caret → icons → border(accent if focused / danger if invalid) → ring? |
| Switch | track fill_rounded(r-full) → knob: shadow-xs → fill_circle → ring? |
| Slider | track r-full → range r-full → ticks? → thumb: shadow → fill_circle → stroke → ring? |
| Select/Dropdown | trigger as Button → content: shadow-lg → fill_rounded(elev,r8) → stroke → per-item: wash? → check → label → shortcut |
| Tabs | list fill_rounded(sunk,r6) → active: shadow-xs + fill_rounded(surface,r6) → labels → panel hairline? |
| Dialog | scrim full-screen → shadow-xl → fill_rounded(elev,r12) → stroke → header → divider? → body → footer buttons |
| Toast | shadow-lg → fill_rounded(elev,r8) → stroke → stripe/glyph → texts → action/close |
| Tooltip | shadow-lg → fill_rounded(text-fill,r6) → label in surface color |
| Progress | track r-full → buffer? → fill r-full (animated width) |
| ScrollArea | content via clip → scrollbar track? → thumb r4 |
| Badge/Avatar | fill (r4 / r-full clip) → glyph/text → ring? |
| Skeleton | fill_rounded(sunk,r4) → clip → sweep band alpha-lerped → reset_clip |

### 4.2 Interaction state machine (shared)

Every interactive widget runs this machine; timers in AnimTicker frames:

```
            key-focus (Tab)              pointer
   ┌─────┐  ──────────►  ┌─────────┐   ┌──────────┐
   │ IDLE │              │ FOCUSED │   │  HOVER   │ (6f fade-in)
   └─────┘  ◄──────────  └────┬────┘   └────┬─────┘
     ▲      blur/Esc          │ activate    │ press
     │                        ▼ (dn)        ▼ (dn)
     │                    ┌─────────────────────┐
     │  release outside   │      ARMED/PRESSED  │── release inside ──► COMMIT
     └────────────────────┤  (flatten 1 level)  │    → on_change(parent)
     disabled ──► ┌──────────┐                  └── key-up outside ──► revert
                  │ DISABLED │  (no transitions, paints static)
                  └──────────┘
```

- COMMIT = state write + parent notification + damage on own bounds (+ring
  margin). Toggle widgets (checkbox, switch, tab, radio) flip their bit on
  COMMIT only — visual preview during ARMED shows the *future* state at 50%
  blend, never the committed one.
- `invalid` is a data flag OR-ed onto paint, not a machine state.
- `open` widgets (select, menu, dialog, tooltip) run a parallel open/closed
  bit; while open they own an L-layer region and a dismissal contract
  (Esc / click-outside → close without commit).

---

## 5. Definition-of-done checklist — "widget is done"

A widget ships when ALL of these hold:

- [ ] **Paints every cell of its state matrix** — normal, hover, pressed/armed,
      focused, disabled, plus data states (checked, invalid, selected, loading,
      placeholder) — verified by a selftest screen cycling each state
      (`kernel/ui/selftest.cc` pattern).
- [ ] **Token-only colors.** `grep` shows no literal `0x` colors in the widget
      `.cc`; everything resolves via `Theme`. Verified in both palettes.
- [ ] **Both palettes.** Painted under light AND dark token sets with no code
      change; legibility checked (borders visible, shadows present, focus ring
      visible on accent fills).
- [ ] **Keyboard contract.** Reachable via Tab in declared order; activation
      keys per §3 entry; focus ring painted; focus trap honored where required
      (dialog); no keys swallowed that belong to parents (returns false).
- [ ] **Parent notification.** Every value commit calls its `on_change`/event
      path; no commit fires during travel/preview — only on release/commit.
- [ ] **Damage discipline.** State transitions mark only own bounds + ring
      margin dirty; animated widgets (spinner, caret, skeleton, indeterminate
      progress) register with `AnimTicker` and unregister on hide.
- [ ] **Clip respect.** Never paints outside `set_clip` region; text/scroll
      overflow clipped inside the widget, not by the parent.
- [ ] **Measure/layout honest.** `measure()` returns real preferred size
      (padding + content + icon slots); `layout()` handles being given less
      (clamps text, keeps hit-rects ≥24px).
- [ ] **Child budget.** Composite widgets stay ≤16 children or use slots/paint
      instead of child widgets; documented which.
- [ ] **Spec cross-check.** Anatomy matches this document's slot names;
      deviations documented in the widget header comment.

---

## 6. Sources

- Button — https://ui.shadcn.com/docs/components/button
- Card — https://ui.shadcn.com/docs/components/card
- Dialog — https://ui.shadcn.com/docs/components/dialog
- Dropdown Menu — https://ui.shadcn.com/docs/components/dropdown-menu
- Tabs — https://ui.shadcn.com/docs/components/tabs
- Select — https://ui.shadcn.com/docs/components/select
- Input — https://ui.shadcn.com/docs/components/input
- Switch — https://ui.shadcn.com/docs/components/switch
- Slider — https://ui.shadcn.com/docs/components/slider
- Toast — https://ui.shadcn.com/docs/components/toast
- Tooltip — https://ui.shadcn.com/docs/components/tooltip
- Badge — https://ui.shadcn.com/docs/components/badge
- Avatar — https://ui.shadcn.com/docs/components/avatar
- Progress — https://ui.shadcn.com/docs/components/progress
- Scroll Area — https://ui.shadcn.com/docs/components/scroll-area
- Separator — https://ui.shadcn.com/docs/components/separator
- Theming / token convention — https://ui.shadcn.com/docs/theming
- Registry inventory & port spec — docs/research/ui-registries.md
- Shell/elevation context — docs/research/desktop-shell.md
