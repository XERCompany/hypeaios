# HypeUI design system — dark-theme spec (tokens, shadows, widget catalog, build order)

Implementation-ready design spec for the ground-up HypeAI OS desktop reskin.
Direction (user reference screenshots): clean dark surfaces ~#0A0A0A–#1A1A1A,
1px hairline borders, restrained crisp shadows (sharp umbra + soft penumbra —
no big blurry glows), Inter/Montserrat-style type with clear hierarchy, one
accent blue ~#3B82F6/#2563EB, 6–8px radii, tight 4px spacing scale. Vocabulary
is ported from shadcn/ui + Radix primitives; calibration cross-checked against
macOS dark, Windows 11 Fluent (Mica), and VS Code Dark+.

Rendering model this spec targets: immediate-mode `Canvas`
(`fill_rect`, `fill_rect_alpha`, `fill_rounded_rect`, `stroke_rounded_rect`,
`fill_circle`, `draw_text`, `set_clip` — `kernel/ui/canvas.h:18-40`) over a
linear framebuffer with rect damage; `Widget` is measure→layout→paint with
≤16 children (`kernel/ui/widget.h:11-36`); animation via `AnimTicker`
(`kernel/ui/anim.h`); token tables in `kernel/ui/theme.h:8-44` +
`kernel/ui/huitokens.h:19-77`; paint helpers `rui_panel/rui_card/
rui_control_bg/rui_shadow` in `kernel/ui/hyperui.h:10-41`; soft-shadow raster
in `kernel/gfx/hypegfx.h:12-17` (`gfx_soft_shadow[_tinted]`).

---

## 1. Design principles

1. **Fill = kind, shadow = elevation.** Fill lightness only distinguishes
   *surface kind* (bg < surface < elevated). Depth is carried by the shadow
   ramp plus the 1px top-edge highlight (Type-3, §3). Kill all shadows → still
   legible via borders; kill all borders → layering still reads via shadows.
2. **Hairlines everywhere.** Every bounded surface gets a 1px `border`
   stroke painted *inside* its bounds. Inputs get `border_strong`. No 2px
   chrome borders, no gradient chrome.
3. **One accent.** Exactly `#2563EB→#3B82F6` blue marks interactivity: focus
   rings, selection washes, primary fill, checked marks, links, progress.
   Never on large surfaces, never on resting borders.
4. **Radii: 4 / 6 / 8 / 12 only.** r4 inline chips+badges, r6 controls,
   r8 cards/menus, r12 dialogs/windows. `full` for pills/knobs/avatars.
5. **Density.** 13px UI text, 28–36px controls, 16px card padding,
   24px dialog padding.
6. **States are painted.** normal/hover/pressed/focused/disabled are explicit
   paint branches; focus is always a painted 2px `ring`, never implied.
7. **Immediate-mode honest.** Paint only own bounds, mark damage on state
   change, cost ∝ visible area. Shadows are pre-baked stop fills, never
   per-frame full-screen blur passes.

---

## 2. Design tokens (target `Theme` field set)

All colors `hcpp::u32` 0xRRGGBB; alphas `u8` 0–255. Token names follow the
shadcn semantic surface/foreground-pair convention (`primary`→`primary_fg`)
[ui.shadcn.com/docs/theming]. Delta vs shipped tables in
`kernel/ui/theme.cc:17-52` / `huitokens.cc:19-51` noted in the right column.

### 2.1 Dark color ramp (primary palette)

| Token | Value | Usage | Shipped today |
| --- | --- | --- | --- |
| `bg` | `#0B0B0E` | desktop/app background, window body | `#0F1113` → darken 1 stop |
| `surface` | `#141418` | cards, panels, inputs at rest | `#1A1D21` (`card`) |
| `surface_sunk` | `#0E0E12` | wells, code blocks, table zebra, sunk track | — new |
| `elev` | `#1B1B22` | flyouts, menus, popovers, dialogs, taskbar | `#22262B` (`secondary`) |
| `border` | `#26262C` | hairlines everywhere (≈ white 9%) | `#2A2E34` |
| `border_strong` | `#3A3A44` | input edges, hover borders | — new (≈ zinc-700) |
| `text` | `#FAFAFA` | primary text/icons | `#F9FAFB` (`fg`) |
| `text_dim` | `#A1A1AA` | secondary text, placeholders, shortcuts | `#9CA3AF` (`muted_fg`) |
| `muted` | `#16161C` | disabled fills, submenu surfaces | `#16191D` (`muted`) |
| `accent` | `#2563EB` | primary fill at rest (blue-600) | `#3B82F6` → rest 1 stop deeper |
| `accent_hover` | `#3B82F6` | accent hover (blue-500; dark UIs lighten on hover — GitHub dark #1F6FEB→#388BFD, VS Code #0E639C→#1177BB) | — new |
| `accent_active` | `#1D4ED8` | accent pressed | — new |
| `accent_fg` | `#FFFFFF` | text/glyphs on accent fills | `#FFFFFF` |
| `accent_soft` | `#3B82F6` @ 18% | hover/selected wash on rows, menu items, tabs | — new (was solid indigo wash) |
| `primary` | `#F5F5F5` | shadcn-style near-white primary button (alt to accent) | `#F9FAFB` |
| `primary_fg` | `#111318` | text on `primary` | `#111827` |
| `destructive` | `#EF4444` | danger actions, invalid borders | `#EF4444` ✓ |
| `destructive_soft` | `#EF4444` @ 14% | destructive hover wash, alert fill | — new |
| `success` | `#22C55E` | confirmations, ok badges | `#22C55E` ✓ |
| `warning` | `#EAB308` | warnings | `#EAB308` ✓ |
| `track` | `#26262C` | slider/progress/switch unfilled track (= `border`) | reuse `border` |
| `ring` | `#3B82F6` @ 55% | 2px focus ring | `#3B82F6` (alpha new) |
| `scrim` | `#000000` @ 55% | modal backdrop dim | — new |
| `shadow` | `#000000` | shadow source color | `#000000` ✓ |
| `top_edge` | `#FFFFFF` @ 7% | Type-3 inner top highlight on elevated dark surfaces | `draw_top_edge` α18 ✓ |
| `chart[5]` | `#3B82F6 #22C55E #EAB308 #EF4444 #A855F7` | data series | unchanged |

Hover/active derivation rule (so widgets need no per-token hover fields):
`hover(fill) = fill mixed 6% toward #FFFFFF`; `active(fill) = 10%`. Accent,
destructive use their explicit `_hover`/`_active` tokens instead.

### 2.2 Light palette (secondary — keep fields aligned)

bg `#F3F4F6`, surface `#FFFFFF`, sunk `#F4F4F5`, elev `#FFFFFF`,
border `#E4E4E7`, border_strong `#D4D4D8`, text `#18181B`, dim `#71717A`,
accent `#2563EB` / hover `#1D4ED8` / active `#1E40AF`, accent_fg `#FFFFFF`,
accent_soft `#3B82F6`@12%, primary `#18181B`, destructive `#DC2626`,
ring `#2563EB`@40%, scrim `#000000`@40%. (Shipped `theme_light()` already
≈ this; only add the `_hover`/`_sunk`/`_strong` fields.)

### 2.3 Shadow recipes — draw-call parameters

Each level = ≤2 `gfx_soft_shadow` stops painted before the surface
(umbra = small/sharp/high-alpha, penumbra = large/soft/low-alpha). Params are
`(dy, blur, alpha%)` of `shadow`. Dark alphas are Tailwind v4 values
×~2–2.5 — dark shadows need more alpha to read (Fluent renders the same
elevation "with a different shadow intensity per theme"
[learn.microsoft.com — Layering and elevation]). Tailwind v4 reference:
`md = 0 4px 6px-1px rgb(0 0 0/.1) + 0 2px 4px-2px /0.1`,
`xl = 0 20px 25px-5px + 0 8px 10px-6px`, `2xl = 0 25px 50px-12px /0.25`
[tailwindcss.com/docs/box-shadow].

| Level | Penumbra (dy,blur,α) | Umbra (dy,blur,α) | Use |
| --- | --- | --- | --- |
| `sh-none` | — | — | inline, sunk wells |
| `sh-xs` | — | (1, 2, .28) | buttons, inputs, badges on color |
| `sh-sm` | (1, 3, .12) | (1, 2, .18) | cards, taskbar strip, panels at rest |
| `sh-md` | (4, 8, .14) | (1, 3, .18) | raised card, sticky header, pressed→rest midpoint |
| `sh-lg` | (8, 16, .16) | (2, 6, .20) | menus, popovers, tooltips, open selects |
| `sh-xl` | (16, 28, .20) | (4, 10, .24) | dialogs, sheets, command palette |
| `sh-2xl` | (24, 44, .30) | (8, 16, .28) | top-level windows, drag-lifted card |

Shipped `hui::shadow()` (`huitokens.cc:69-85`) is a single-stop ramp
`xs(1,2,.04)…2xl(24,64,.14)` — keep the enum, retune to the two-stop table
above and raise dark alphas as shown (its +25% dark bump becomes this
explicit table). `rui_shadow` clamps blur ≤40px and grows damage via
`rui_paint_bounds` spill 42/56px (`hyperui.h:26-33`) — keep those caps;
sh-2xl penumbra fits inside them.

**Elevation law:** rest never above `sm`; overlays never below `lg`;
press drops one level; hover raises controls `xs→sm` only (cards/menus do
NOT float on hover). Matches Fluent control elevation rest 2 / pressed 1 /
card 8 / flyout 32 / tooltip 16 / dialog-window 128 with 1px contour stroke
[Microsoft Learn, Layering].

### 2.4 Radius scale

| Token | px | Use |
| --- | --- | --- |
| `r4` | 4 | badge, checkbox, kbd, tag, scroll thumb |
| `r6` | 6 | buttons, inputs, selects, switches, menu items, tab pills |
| `r8` | 8 | cards, panels, tables, tooltips, flyout content |
| `r12` | 12 | windows, dialogs, sheets, drawers |
| `r-full` | h/2 | pills, avatars, switch knob, progress caps, task buttons |

Shipped `radius_sm/md/lg = 6/8/12` (`theme.cc:44-46`) — add `r4`; window
chrome uses `r12` (top corners only on snapped windows).

### 2.5 Spacing scale (4px base)

`s0=0 s1=4 s2=8 s3=12 s4=16 s5=20 s6=24 s8=32 s12=48 s16=64`.
Insets written `pad(x,y)`: control-md = `pad(s3,s2)`, card = `pad(s4)`,
dialog = `pad(s6)`, menu item = `pad(s3,s2)`, flyout section gap = `s4`.
Shipped `spacing_xs/sm/md/lg = 2/4/8/16` — replace with the full scale
or alias `xs→s1/2, sm→s1, md→s2, lg→s4` at call sites first.

### 2.6 Typography scale

One family (Inter/Montserrat-style grotesk; `Theme::resolved_font()`,
`theme.h:43`). Sizes = pixel heights; weights map to font cuts — synthesize
600 by stem-bolden only as last resort.

| Token | px | Weight | Line-h | Use |
| --- | --- | --- | --- | --- |
| `t-xs` | 11 | 500 | 1.4 | badges, kbd, captions, toast meta |
| `t-sm` | 12 | 400/500 | 1.4 | descriptions, placeholders, menu shortcuts |
| `t-base` | 13 | 400/500 | 1.4 | UI default (shipped `text_size=13` ✓) |
| `t-md` | 14 | 400/500 | 1.45 | emphasized body, card description |
| `t-lg` | 16 | 600 | 1.3 | card/dialog title, window caption |
| `t-xl` | 20 | 600 | 1.25 | section heading, flyout title |
| `t-2xl` | 24 | 600 | 1.2 | page heading |
| `t-num` | 32 | 600 | 1.1 | KPI/clock digits (QS clock, numticker) |

Disabled text = `text_dim` at full alpha (dim by color, never by stacking
alpha — alpha-stacked text shimmers on repaint).

### 2.7 Elevation / z-layer model

Paint order inside `desk_paint` (`kernel/win/deskman.h:9`); each layer gets
a max shadow level:

| Layer | Contents | Shadow | Owner |
| --- | --- | --- | --- |
| L0 | wallpaper/desktop fill | — | `kernel/win/deskbg.cc` |
| L1 | desktop icons | xs | `kernel/win/deskicons.cc` |
| L2 | windows (normal→2xl focused/dragged) | md–2xl | `kernel/win/winman.cc` |
| L3 | deskbar strip (36px) | sm | `kernel/ui/deskbar.cc` |
| L4 | flyouts: start menu, QS, NC, calendar, clipboard, ctx menus | lg | `deskbar.cc`, `winman.cc` `wm_ctx_*` |
| L5 | modal dialogs + scrim over L0–L4 | xl | `widgets/dialog.cc`, `overlays/overlay.cc` |
| L6 | toasts | lg | `overlays/toast.cc` (above taskbar) |
| L7 | tooltips, hover-cards | lg | new `widgets/tooltip.cc` |
| L8 | drag ghost, alt-tab switcher, cursor | 2xl | `winman.cc` alttab/drag |

User-required order desktop < window < flyout < toast < tooltip < cursor is
preserved; dialogs sit between flyouts and toasts by modality (a modal
blocks everything below L5).

### 2.8 Motion (AnimTicker frames @ ~60fps, 4 ticks/frame)

hover 6f in / 9f out linear · press/focus instant · menu open 8f fade+4px
rise · dialog 9f scrim+scale-in 2% · toast 10f in / 8f out slide 16px ·
switch knob 6f · progress indeterminate 90f loop · caret blink 30f period.

---

## 3. Shadow theory — Type 1 / 2 / 3 (per user "Visual Tutorial" reference)

| Type | Recipe | Where in HypeAI OS |
| --- | --- | --- |
| **Type 1 — glow** | light-colored drop shadow: `fill_rounded_rect` dilated, blurred, in a *light* color (white or accent), painted behind the object | Sparingly only: accent glow under the active taskbar button pill / focused Start button (`accent` @ 25%, dy0, blur10), boot splash logo, selected desktop icon halo (`text`@12%, blur8). **Never** on windows/menus — reads neon. Implement as `rui_glow(canvas, rect, radius, rgb, α)` next to `rui_shadow`. |
| **Type 2 — realistic** | darker-than-background drop shadow: umbra+penumbra stops of `shadow` (#000) per §2.3 | Default for every elevated surface: windows (2xl), toasts (lg), menus (lg), dialogs (xl), drag ghost (2xl). This is `hui::draw_shadow` → `gfx::gfx_soft_shadow`. |
| **Type 3 — hybrid** | Type 2 shadow **plus** a 1px light line at the top inner edge of the surface (simulates light catching the elevated edge; Fluent's "brighter top elevation border in dark" is the same trick) | Every card/panel/flyout/dialog in dark mode. Already shipped: `hui::draw_top_edge()` paints a 1px `FFFFFF` α18 line (`huitokens.cc:114-125`) — keep, but route ALL elevated surfaces through `rui_panel` so none forget it. |

Decision rule: dark surface on dark background → Type 3; floating surface
that must read as *above chrome* (toast over taskbar, tooltip over window) →
Type 2 with border; deliberate attention accent → Type 1, ≤1 per screen
region. Windows while dragged get Type 2 at 2xl (lift reads instantly).

---

## 4. Calibration from other dark systems

| System | Practical values extracted | What we take |
| --- | --- | --- |
| shadcn/ui dark (zinc) | `--background oklch(.145)` ≈ #171717, `--card .205` ≈ #1E1E1E, `--popover .269` ≈ #242424, `--border` white/10%, `--ring .556`, `--radius .625rem` | semantic token names + border-as-alpha convention [ui.shadcn.com/docs/theming] |
| macOS dark | window bg #1E1E1E (measured #1E1F20–#202226 with desktop tint), systemBlue #0A84FF, separator white~10%, huge soft window shadow | surfaces slightly *bluer* than pure gray; window shadow = biggest single penumbra [swiftuicolors.com, cupertino-ui.com] |
| Fluent / Win11 dark | bg #202020 Mica tint, card #FFFFFF0D (~#191919@solid), layer #3A3A3A4C, elevation values 1/2/8/16/32/128 + 1px stroke, **dark elevation border = brighter top edge**, accent #76B9ED | numeric z-elevation ladder → §2.7; brighter-top-edge validates Type 3 [learn.microsoft.com layering/mica] |
| VS Code Dark+ | editor #1E1E1E, sidebar #252526, fg #D4D4D4, muted #858585, selection #264F78, accent #007ACC, terminal blue #3B8EEA | text doesn't need to be #FFF — `#FAFAFA` for primary, `#A1A1AA` dim is already brighter than VS Code; selection wash ≈ accent@18% [dark_plus.json] |

Net calibration: our ramp sits one stop *darker* than all three (user's
#0A0A0A request), accent identical hue family to macOS/VS Code blue.

---

## 5. Widget catalog — all shadcn primitives → `kernel/ui/widgets/`

Column **File**: ✓ = exists, NEW = to create. **Maps to/replaces**: the OS
surface or shipped helper it supersedes. Effort S ≤½day, M 1–2d, L >2d
(state machine + overlay + scroll). States column lists the shared machine
cells needed beyond normal (H=hover P=pressed F=focus-ring D=disabled).

| shadcn primitive | Purpose (1 line) | File | Maps to / replaces | States | Effort |
| --- | --- | --- | --- | --- | --- |
| accordion | stacked collapsible sections | NEW `accordion.cc` | settings pages, NC groups | H,P,F + open | M |
| alert | inline status banner (icon+title+desc) | ✓ `alert.cc` | in-window warnings | — | S (variants audit) |
| alert-dialog | modal confirm, explicit choice | `dialog.cc` + kind | destructive confirms (close/delete) | F trap | S |
| aspect-ratio | fixed-ratio content box | NEW `aspectratio.cc` | image/video app canvases | — | S |
| avatar | round user/system glyph | NEW `avatar.cc` | login (`flowlogin`), account chip in tray | D | S |
| badge | pill status/chip + dot variant | ✓ `badge.cc` | tray unread count (`paint_bell_glyph`), task pills | — | S |
| breadcrumb | path segments + separators | NEW `breadcrumb.cc` | hxfiles path bar | H,F | S |
| button | labeled action, 6 variants | ✓ `button.cc` | `rui_control_bg` call sites, dialog footers, chrome buttons | H,P,F,D,loading | S (add ghost/link/size ladder) |
| button-group | joined segmented buttons | NEW `buttongroup.cc` | QS toggles row, toolbar clusters | per-seg H,P | S |
| calendar | month grid w/ nav | NEW `calendar.cc` | deskbar cal flyout (`paint_cal` deskbar.cc:1749) — extract | H,F + today | M |
| card | bounded surface w/ header/content/footer | `panel.cc` → rename `card.cc` | `rui_panel`/`rui_card` call sites (deskbar:1408.., toast.cc:107, apps) | data-invalid | S |
| carousel | paged content + prev/next | NEW `carousel.cc` | wallpaper picker, login user picker | H,P + page anim | M |
| chart | styled series render | ✓ `chart.cc` | perf monitors, `chart[5]` tokens | — | done |
| checkbox | tri-state check box+label | ✓ `checkbox.cc` | settings toggles, file-picker selection | H,P,F,D + indet | done |
| collapsible | single show/hide section | NEW `collapsible.cc` | accordion base, "details" rows | H,F + open | S |
| combobox | input + filtered listbox | NEW `combobox.cc` | font/app pickers | all + open | L |
| command | palette: input + grouped results | NEW `command.cc` | Start-menu search (`paint_search_field`/menu deskbar.cc:1338-1490) | all + open | L |
| context-menu | right-click item menu | NEW `contextmenu.cc` | `wm_ctx_open/WmCtxItem` (`winman.h:52-62`), deskicon menus | H,F + sub | M |
| data-table | sortable dense table + toolbar | `table.cc` + sort/select | process/file lists | H,F + sel,sort | L |
| date-picker | input + calendar popover | NEW `datepicker.cc` | clock settings | all | M |
| dialog | modal surface + scrim + focus trap | ✓ `dialog.cc` + `overlays/overlay.cc` | app modals, `dialog_dev.cc` | F trap, Esc | S (scrim+anim) |
| drawer | bottom/side slide-in panel | NEW `drawer.cc` | mobile-style sheets, NC variant | open anim | M |
| dropdown-menu | anchored item menu w/ checkbox/radio/sub | NEW `dropdownmenu.cc` | window menu (`wm_menu_*`), Start ctx, tray menus | H,F + sub | L |
| empty | empty-state block (icon+title+cta) | ✓ `empty.cc` | empty folders, NC "no notifications" | — | done |
| field | label+control+hint+error stack | ✓ `field.cc` | all settings rows | invalid | done |
| form | field set + validation wiring | `field.cc` convention | settings pages | invalid | S (doc only) |
| hover-card | rich preview on hover | NEW `hovercard.cc` | task-button window preview | delay-armed | M |
| input | single-line text field | ✓ `textinput.cc` | Start search field, dialogs | H,F,D,invalid,sel | S (icon slots) |
| input-group | input + prefix/suffix addons | `textinput.cc` slots | search field w/ glyph, units | — | S |
| input-otp | segmented code entry | ✓ `inputotp.cc` | PIN/pairing dialogs | F,invalid | done |
| item | row: icon+title+desc+trailing | ✓ `item.cc` | NC rows (`paint_nc_row` deskbar.cc:1593), menu rows, list cells | H,P + sel | done |
| kbd | keyboard-key chip | NEW `kbd.cc` | shortcut hints in menus/settings | — | S |
| label | control caption text | ✓ `label.cc` | fields, settings | — | done |
| listbox | flat option list w/ highlight | ✓ `listbox.cc` | select content, clipboard flyout (`paint_cv` deskbar.cc:1706) | H,F + sel | S (as select content) |
| menubar | persistent top menu row | NEW `menubar.cc` | window menu strip, app menus | H,F + open | L |
| navigation-menu | link list w/ dropdown panels | `menubar.cc` variant | Start "all apps" nav | — | M |
| pagination | page dots/arrows | NEW `pagination.cc` | file-manager paging, carousel dots | H,P,D | S |
| popover | anchored free-form overlay | NEW `popover.cc` | tray glyph flyouts base (`paint_qs/nc/cal/cv`) | open, outside-dismiss | M |
| progress | determinate/indeterminate bar | ✓ `progress.cc` | file ops, boot progress, QS meters | indet | done |
| radio-group | exclusive option set | ✓ `radiogroup.cc` | theme/resolution pickers | H,P,F,D | done |
| resizable | draggable split panes | NEW `resizable.cc` | window edge-resize grips (`winman.cc` resize), split apps | H,P + drag | L |
| scroll-area | clipped viewport + overlay scrollbars | NEW `scrollarea.cc` | every list/flyout (start menu results, NC history) | hover thumb | L |
| select | trigger + popup listbox | NEW `select.cc` | settings dropdowns (theme, vdesk count) | all + open | L |
| separator | 1px semantic divider | ✓ `separator.cc` | menu/toolbar groups, card footer | — | done |
| sheet | edge-anchored dialog panel | NEW `sheet.cc` | notification center reskin (right-edge sheet) | scrim + slide | M |
| sidebar | app nav pane w/ collapse | NEW `sidebar.cc` | hxfiles nav pane, settings nav | H,P + sel | L |
| skeleton | shimmer placeholder | ✓ `skeleton.cc` | list/loading placeholders | anim | done |
| slider | track+range+thumb value drag | NEW `slider.cc` | QS volume/brightness (`paint_slider` deskbar.cc:1493) — extract | H,P,F,D + grab | M |
| sonner/toast | stacked auto-dismiss notices | ✓ `overlays/toast.cc` (ToastHost) | `deskbar_notify` pushes | enter/exit,hover-pause | S (stripe+action slot) |
| spinner | indeterminate activity arc | ✓ `spinner.cc` | busy buttons, boot | anim | done |
| switch | on/off knob toggle | ✓ `switch.cc` | QS toggles, settings | H,P,F,D | done |
| table | grid w/ header rows | ✓ `table.cc` | data-table base | zebra | done |
| tabs | segmented content switcher | ✓ `tabs.cc` | settings pages, app tabs | H,P,F + active | done |
| tags-input | token chips + editor | ✓ `tagsinput.cc` | search filters | F,D | done |
| textarea | multi-line input | NEW `textarea.cc` | notes/terminal input | all + scroll | M |
| toggle | pressed-state button | NEW `toggle.cc` | formatting toolbars | H,P,F + on | S |
| toggle-group | exclusive/multi toggle set | `toggle.cc` × group | QS toggle grid | — | S |
| tooltip | delayed hover label | NEW `tooltip.cc` | task buttons, tray glyphs, chrome buttons | delay, fade | M |
| (custom) marquee/stepper/numticker | ticker/seq helpers | ✓ shipped | clock, boot steps | — | done |

Non-widget primitives stay in the shell layer: `menubar`/`dropdown-menu`
share one `MenuContent` implementation (anchored `elev` r8 lg-shadow box —
same paint contract as `wm_ctx_*` today); `popover` is the shared base for
all four deskbar flyouts; `command` IS the Start-menu body.

Sub-parts follow Radix anatomy: Select = Trigger{value,chevron} +
Content{Item{check-slot,label}}; Slider = Track+Range+Thumb; Dialog =
Scrim+Content{Header{Title,Desc,Close},Body,Footer}; Menu = Trigger +
Content{Label,Item,Separator,CheckboxItem,RadioGroup,Sub}. Slot rect names
go in each widget's header comment.

API sketch pattern (match `Button` in `widgets/button.h:13-40`):
`class X : public Widget` with `set_*(…)` mutators, `measure/layout/paint`
overrides, `handle_key` for the §5 key contract, `on_change` parent notify
via `Widget::parent()`; composites use slots not children when >16 parts.

---

## 6. Shared interaction state machine

All interactive widgets run: IDLE →(Tab) FOCUSED, →(pointer) HOVER(6f) →
ARMED/PRESSED(−1 shadow level) → release-inside COMMIT (value write +
parent notify + damage own bounds+ring margin) / release-outside REVERT.
DISABLED paints static, no transitions. Toggle widgets preview future state
at 50% blend during ARMED, commit on release. `open` primitives (select,
menus, popover, dialog, tooltip) hold a parallel open bit + dismissal
contract (Esc / outside-press → close, no commit); while open they own an
L-layer region (§2.7). `invalid` is a data flag OR-ed into paint.

Definition of done per widget: paints every state-matrix cell on a
`selftest.cc` screen; zero literal colors (`Theme`/`hui` tokens only);
verified under both palettes; full keyboard contract; damage = own bounds;
never paints outside `set_clip`; ≤16 children or slots; slot names match §5.

---

## 7. Redesign build order

| # | Step | Files touched | What lands |
| --- | --- | --- | --- |
| 1 | Token v2 | `kernel/ui/theme.h:8-44`, `theme.cc:17-87`, `huitokens.cc:19-85` | add `surface_sunk`, `border_strong`, `accent_hover/active`, `accent_soft`, `scrim`, `ring_alpha`; retune `k_dark` to §2.1; replace 1-stop `HuiShadow` ramp with §2.3 two-stop umbra+penumbra |
| 2 | Paint layer | `kernel/ui/hyperui.cc:166-242`, `hypegfx.h` | `rui_shadow` → two-stop; new `rui_glow` (Type 1); `rui_panel` keeps Type-3 `draw_top_edge` on every elevated surface; new `rui_menu_box()` = elev+r8+border+sh-lg shared by all L4 surfaces |
| 3 | Widget reskin | `kernel/ui/widgets/*.cc` | token-only audit vs §5 states; Button gains ghost/link/size ladder; `panel.cc`→`card.cc` rename; add `slider.cc`, `scrollarea.cc`, `tooltip.cc`, `dropdownmenu.cc`, `contextmenu.cc`, `select.cc`, `popover.cc`, `calendar.cc`, `command.cc` in that priority order |
| 4 | Deskbar reskin | `kernel/ui/deskbar.cc` (paint_* at :1176-1761) | strip → `surface`+`sh-sm`+top hairline; start button/task pills → `Button ghost`+accent pill (Type-1 accent glow, one per bar); start menu → `command`+`popover`; QS sliders → `slider.cc`, toggles → `switch`; NC → `sheet`/`item` rows; cal/cv flyouts → `calendar`/`listbox` inside `popover`; all five `rui_panel` call sites → shared `rui_menu_box` |
| 5 | Window chrome | `kernel/win/winman.cc:140-200,475-575` | chrome strip h32 → `surface` fill + bottom hairline; caption → `t-lg`/`text_dim`(unfocused); ctl buttons → `IconButton ghost` via `rui_control_bg` (close hover → `destructive` ✓ already); window frame → `rui_panel(rui_elev_window)` sh-2xl + r12 + top-edge; `wm_ctx_*` → `contextmenu.cc`; snap preview → `accent`@15% + r8 outline |
| 6 | Desktop | `kernel/win/deskbg.cc`, `deskicons.cc` | bg → `bg` token; icon label chips → r4 `elev`@80%; selection → `accent_soft` + Type-1 halo |
| 7 | Apps | `kernel/hxrt/apps/{hxabout,hxfiles,revdbg,speedstart}.cc`, `kernel/session/session.cc`, `kernel/ui/flowlogin.cc` | replace raw `rui_panel`/literals with Card/Button/Field widgets; login → avatar+inputotp |
| 8 | Overlays | `kernel/ui/overlays/toast.cc`, `overlay.cc` | toast → elev r8 `sh-lg` + 3px kind stripe + action slot (sonner anatomy); overlay scrim → `scrim` token + 9f fade |
| 9 | Verify | `kernel/ui/selftest.cc` | per-widget state-matrix screens; both palettes; serial marker `hui reskin=ok` |

Order rationale: tokens first so every later diff is mechanical; `rui_*`
before widgets since widgets call it; deskbar+winman after widgets so
flyouts compose real primitives instead of bespoke paint; apps last —
they're pure consumers. Each step compiles+selftests independently.

---

## 8. Sources

- shadcn/ui theming & token pairs — https://ui.shadcn.com/docs/theming
- shadcn/ui component index — https://ui.shadcn.com/llms.txt; registry catalog (57 primitives)
- Radix primitive anatomy — https://www.radix-ui.com/primitives/docs/overview/introduction
- Tailwind v4 box-shadow scale — https://tailwindcss.com/docs/box-shadow
- Windows 11 layering & elevation values — https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/layering
- Fluent Mica material — https://learn.microsoft.com/en-us/windows/apps/design/style/mica
- Fluent dark palette (card/layer/elevation-stroke hexes) — https://rickylabs.github.io/netscript/design-system/themes/fluent/
- VS Code Dark+ colors — https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_plus.json; https://editorthemes.com/themes/vscode-defaults/dark-plus
- macOS dark window bg / measured values — https://swiftuicolors.com/macos-colors/window-background; https://www.cupertino-ui.com/docs/sources
- Codebase: `kernel/ui/theme.h|cc`, `huitokens.h|cc`, `hyperui.h|cc`,
  `canvas.h`, `widget.h`, `anim.h`, `widgets/`, `overlays/`, `deskbar.cc`,
  `kernel/win/{winman,deskbg,deskicons,deskman,xau}`.
