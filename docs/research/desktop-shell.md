# Desktop Shell — implementation research (taskbar, start menu, WM UX, tray, power)

Scope: a Windows/Ubuntu-class desktop shell for hypeaios on top of the
existing immediate-mode stack — `winman` window slots + z-order + damage
tracking (`kernel/win/winman.cc`), `deskman` desktop compositor pass
(`kernel/win/deskman.cc`), wallpaper (`kernel/win/deskbg.cc`), overlay host +
toasts (`kernel/ui/overlays/`), theme tokens (`kernel/ui/theme.h`,
`kernel/ui/hyperui.h`), app registry (`kernel/hxrt/hxrt.h`), VFS app dir
`HYPEOS://U/apps` (`kernel/vfs/hypefile.cc`), RTC (`kernel/time/rtc.h`),
PS/2 pointer routing (`kernel/input/hypeptr.cc`) and scancode decode
(`kernel/input/scancode.h`). Single framebuffer, no GPU compositor, no
per-window backbuffers — everything below is chosen to map onto rect-based
damage, not onto surface compositing.

Sources consulted: Microsoft Win32 UX guidelines (taskbar, notification
area, menus, window management), freedesktop Desktop Notifications
Specification 1.3, GNOME Shell design wiki + gnome-shell Sass metrics,
Ubuntu Dash-to-Dock stylesheet, OSDev wiki (graphics stack, compositing),
Wayland architecture docs, AltSnap / react-ui-os snap-zone code.

---

## 1. Taskbar / dock anatomy

**What exists**

- Windows taskbar is a single shell-owned bar: Start button (left/center),
  pinned + running taskbar buttons, notification area (overflow chevron,
  status icons, clock), "show desktop" sliver at the far right. Windows 10
  default height is 40 px at 100% DPI; Windows 11 raised it to 48 px with
  centered buttons. Classic grouped buttons cap at ~160 px wide before
  collapsing to icon-only; ungrouped (Win7+ default) buttons are icon-first
  48-62 px wide. Buttons have four visual states: normal, hover
  (lifted/underlight), active/pressed (accent underline + darker fill),
  attention (amber flash, ~3-5 pulses when a background window requests
  foreground).
  <https://learn.microsoft.com/en-us/windows/win32/uxguide/winenv-taskbar>
- Windows taskbar is itself a topmost window (`Shell_TrayWnd`, owned by
  explorer) that registers as an *appbar* (`SHAppBarMessage`/`ABM_NEW`):
  the OS shrinks the **work area** so maximized windows and snap targets
  never overlap the bar. This is the single most important structural
  idea: bar = reserved work-area edge, not a floating window.
- GNOME top bar: full-width, `$panel_height: 2.2em` (~33-36 px),
  Activities button left, clock center, status icons + system menu right;
  contents are entirely system-owned, panel becomes transparent on the
  overview/lock screen. Ubuntu dock (Dash-to-Dock): icon dock (default
  icon size 48 px, `$dash_padding` 10-12 px, `$base_margin` 4 px,
  `$dock_icons_distance` 4 px) on the left edge, running indicators as
  dots, no window titles.
  <https://github.com/GNOME/gnome-shell/blob/master/data/theme/gnome-shell-sass/widgets/_panel.scss>
  <https://github.com/ubuntu/dash-to-dock/blob/master/_stylesheet.scss>
- Hover/hot tracking: both shells keep a per-button state machine
  {normal, hover, pressed, running-focused, running-unfocused, flashing}.
  Windows flashes a taskbar button when a window calls
  `FlashWindow`/`FlashWindowEx`; GNOME uses a "demands attention" dot.

**Metrics worth copying**

| Element | Windows 10 | Windows 11 | GNOME/Ubuntu | Suggested |
| --- | --- | --- | --- | --- |
| Bar height | 40 | 48 | ~34 top / 48+pad dock | 44 |
| Start button | 40-48 sq | 40 sq | text label | 40 sq |
| Task button max w | ~160 (labeled) | icon-only | icon-only | 148 |
| Task button min w | ~44 | 40 | 48 | 40 |
| Tray icon | 16 | 16 | 16-24 | 16 |
| Clock width | ~64-88 | ~64 | centered | 72 |
| Inner padding | 4-8 | 6-8 | 6 (base_padding) | 8 |

**Apply to HypeAI OS:**

- Add `kernel/win/taskbar.cc` (+`.h`) as a *z-band above* `wm_paint`, not a
  `Win` slot: paint it inside `desk_paint` after `wm_paint`
  (`deskman.cc:53`), before `overlays.paint_overlays`. Reserve work area
  by subtracting a `constexpr i32 taskbar_h = 44` band from the region
  `wm_open` cascades into (`winman.cc:325-330`) and from every future
  maximize/snap rect.
- Task buttons = iterate `collect_order`-style over `slots[]`
  (`winman.cc:291-300`), draw one `rui_control_bg` chip per window with
  `Win::app_name` (24-char cap already fits ~160 px at 13 px text);
  focused chip gets `theme.accent` 2 px underline like the chrome accent
  strip in `paint_chrome` (`winman.cc:229-231`).
- Route clicks first to taskbar hit-test in `wm_handle_mouse`
  (`winman.cc:484`) — bar rect checked before descending-z window loop.
  Click on a button: `wm_focus(id)`; click on focused window's button:
  minimize (needs `WinState::Minimized`, §3).
- Clock: `hype::time::rtc_format` (`kernel/time/rtc.h`) already produces
  `"YYYY-MM-DD HH:MM:SS"`; render HH:MM in the tray cell and re-invalidate
  only the clock rect once per minute via `wm_invalidate(clock_rect)`
  (`winman.cc:439`) — cheap damage, no full repaint.
- Attention flash: add `hcpp::u8 flash_ticks` to `Win`; when nonzero, the
  taskbar paint alternates `theme.warning` fill and decrements — reuse
  the toast tick countdown pattern (`kernel/ui/overlays/toast.h`,
  `Toast::remaining`).

---

## 2. Start menu / Activities overview

**What exists**

- Windows 10 menu: two-pane — left a scrollable all-apps list
  (alphabetical, index headers), right a pinned live-tile grid. Windows
  11: centered single panel — search box on top, pinned grid (~18 icons,
  6x3) middle, "All apps >" expands to alphabetical list, recommended row
  below, power + user at the bottom. Both are *search-first*: typing
  while the menu is open filters app names instantly; Enter launches the
  top hit.
- GNOME Activities overview: full-screen dim layer; hot corner + Super
  key + Activities button all open it; centered search field, app grid
  (paginated, alphabetical), workspace strip on the right, running-window
  thumbnails center. Overview is "a facilitator, not a destination" —
  Escape or selecting anything dismisses it.
  <https://wiki.gnome.org/Projects/GnomeShell/Design/>
- Search-first wins on a real shell: the list enumeration is trivial (we
  have `hxrt_list()` up to `hxrt_max_apps = 32`), substring match on
  `HxerHeader::name` is free, and it avoids maintaining two separate
  "pinned" vs "all apps" models at v1.
- Positioning conventions: Windows 10 menu anchors to the Start button,
  grows upward-left; Windows 11 centers it; GNOME overview is always
  centered/full-screen. A bottom-anchored popover above the taskbar is
  the smallest delta from our existing `WinMenu` overlay widget
  (`winman.cc:160-205`).

**Apply to HypeAI OS:**

- `SpeedStart` (`kernel/hxrt/apps/speedstart.cc`) is already 80% of a
  start menu: search field (`search_h = 30`), alphabetical grid over
  `hxrt_list()` (`pad=16, grid_gap=14, cell_w=92, cell_h=100,
  icon_size=52`), monogram icons, launch via `desk_open_app`
  (`speedstart.cc` tiles → `deskman.cc:19-21`). Keep it as the
  implementation; the shell change is *how it opens*: taskbar Start
  button + Super key should open it as a bottom-anchored overlay (GNOME
  style, no window chrome) instead of a cascaded `wm_open` window.
- Model it on `WinMenu` (`winman.cc:160-205`): an `OverlayHost`-hosted
  `Widget` sized ~560x420, anchored at `{x=8, y=taskbar_top-height-8}`,
  closed by Escape or click-outside. `menu_rect_now()`
  (`winman.cc:111-128`) already shows the clamp-to-screen pattern.
- Search input: SpeedStart has a painted search box but key routing is
  not wired — `wm_handle_key` (`winman.cc:526-536`) only forwards to
  overlays when one is open, so an overlay-mode Start panel gets keyboard
  focus "for free" once ASCII events are dispatched (see §7 on key
  routing; `kernel/input/scancode.h` `DecodedKey`/`scancode_last_ascii`).
- Launch action inside the overlay must call `wm_open(name, Rect{})`
  then close the overlay + `wm_invalidate_all()` (`winman.cc:443`) — the
  menu layer must not keep a damage clip while a new window paints.
- App enumeration source of truth: `hxrt_list()`/`hxrt_count()`
  (`kernel/hxrt/hxrt.h`) for builtin `.hxer` apps; merge in
  `HYPEOS://U/apps/*.hxer` entries via `hype::vfs::fs_list`
  (`kernel/vfs/hypefile.cc:112` seeds the dir; `main.cc:510` lists pack
  dirs) so user-installed apps appear next to builtins — the freedesktop
  `.desktop`-in-`applications/` equivalent.

---

## 3. Window management UX

**Minimize / maximize / restore semantics**

- Windows keeps a `restore rect` per window: maximize expands to the work
  area (screen minus taskbar/appbars) and remembers the floating frame;
  restore/drag-away puts it back. Minimize removes the window from the
  visible set but keeps it alive — it stays in Alt-Tab (sorted to the
  bottom of z-order) and on its taskbar button.
- Win7+ semantics worth copying: drag a *maximized* window's title bar
  downward → unmaximize and continue dragging (the cursor re-anchors
  proportionally in the restored frame). Double-click title bar toggles
  maximize. Title-bar context menu exposes Restore / Move / Size /
  Minimize / Maximize / Close.
  <https://learn.microsoft.com/en-us/windows/win32/uxguide/win-window-mgt>

**Aero Snap**

- Pointer-driven edge zones (from AltSnap `hooks.c` / react-ui-os /
  makepad `snap.rs`, all convergent): during a title-bar drag, evaluate
  the pointer against the work area each move:
  - within ~16-24 px of left edge → left half; right edge → right half;
    top edge (tighter, ~10-16 px) → maximize
  - within ~2x the threshold of a corner → quarter of the screen;
    corners take precedence over edges
  - bottom edge does nothing on Windows (bottom quarters only exist in
    Win11 Snap Layouts / extensions)
- Show a translucent accent "snap preview" rectangle while hovering a
  zone; commit the rect on button release. Win+Arrow keys perform the
  same ops keyboard-side (Left/Right = halves, Up = maximize, Down =
  restore/minimize; a second press after a half-snap moves to quarters).
- Snapped and maximized rects both store a restore rect so dragging out
  or Win+Down restores geometry.

**Alt-Tab switcher**

- Ordering is the window **z-order** (≈ MRU; topmost windows first,
  minimized last) — repeated Alt+Tab toggles the two most recent windows.
  Hold Alt, press Tab to advance, Shift+Tab to reverse, Esc cancels,
  releasing Alt commits. Windows 7+ shows thumbnail previews; the
  original Win3.1 style was a small centered box with only icon+title.
  <https://en.wikipedia.org/wiki/Alt-Tab>
- Modern row layout: horizontal strip centered vertically, one
  icon/thumbnail + label per window, selection box with accent border;
  first Tab selects the item *after* the current window.

**Virtual desktops**

- Win10 Task View / GNOME workspaces: N desktops, each an independent
  window set + z-order; taskbar and Alt-Tab filter to the current
  desktop; shortcuts Win+Ctrl+Left/Right (move), Win+Ctrl+D (new),
  Super (overview). GNOME keeps workspaces vertical and dynamic (always
  one empty trailing workspace). Not v1 for us, but the data model
  costs nothing extra: a `desktop` field per `Win` and a current-desktop
  index, filtered in `collect_order`.

**Apply to HypeAI OS:**

- Extend `WinState` (`winman.h:14`) with `Minimized` and `Maximized`, add
  `Rect restore` to `Win` (`winman.h:16-24`); `wm_paint`'s loop
  (`winman.cc:409-435`) skips Minimized frames entirely (no damage), and
  `wm_focus` on a minimized window restores it first.
- Add `wm_minimize(id)`, `wm_toggle_max(id)`, `wm_snap(id, zone)` —
  all three are just `w.frame = rect; w.content_rect = content_for(rect);
  damage_add(old); damage_add(new);` — the existing
  `damage_add`/`wm_invalidate` path (`winman.cc:50-56, 439-443`) already
  covers repaint for both vacated and occupied regions.
- Work-area helper: `Rect work_area()` = screen minus taskbar band —
  the single rect maximize and all snap zones derive from, matching the
  `ABM_*` work-area model.
- Drag engine: `wm_handle_mouse` currently only acts on press
  (`winman.cc:484-486`); add press-in-chrome-strip → begin drag state
  (store `drag_win`, `grab_offset`), continue on `pressed=false` in
  `route_buttons` (`hypeptr.cc:65-70`) and on motion via `CurStat` deltas
  (`kernel/input/curstat.h`). While dragging, evaluate the snap zone each
  frame and paint the preview rect as an accent-alpha fill in `wm_paint`
  before overlays — pure damage, no buffers needed.
- Resize grip: bottom-right 16x16 hit zone on `w.frame`; same drag
  state machine, mutates `w`/`h` with min clamp (~160x120).
- Alt-Tab v1 = the Win3.1 style: overlay panel (reuse `OverlayHost`) with
  the z-order list from `collect_order(order, false)`
  (`winman.cc:291-300`), selection index, painted as a `rui_panel` box
  centered ~360x48 with per-window icon dot + `app_name`. Needs modifier
  state: `scancode_key_pressed` (`kernel/input/scancode.h`) plus a
  key-code for LeftAlt in `kernel/istatekeys.h`. Skip thumbnails — we
  have no per-window backbuffers to snapshot.
- The title-bar menu already exists (`menu_items` = Move/Minimize/Close,
  `winman.cc:36-37`); extend to {Restore|Maximize, Minimize, Move,
  separator?, Close} and bump `menu_item_count`/`menu_item_h` — see §4
  for menu metrics.
- Selftest additions in `wm_selftest` (`winman.cc:559-611`): open two
  windows, minimize A, verify `wm_focused()==B` and A skipped in paint;
  snap B left, verify `frame == left half of work_area`; Alt-Tab overlay
  cycle returns expected order.

---

## 4. Desktop icons + context menus

**What exists**

- Desktop icons live on an invisible snap grid: Windows default cell is
  ~75x75 logical px (`IconSpacing`/`IconVerticalSpacing`, registry range
  -480..-2730 scaled x10 → effective 75-112 px cells); icon is 32-48 px
  centered in the cell top, label wraps to 2 lines below in small text,
  selection = translucent accent rect + highlight. Filling order is
  column-major starting top-left; "align to grid" snaps on drop,
  auto-arrange re-flows.
- Context menus (Microsoft menu guidelines): hierarchical command lists;
  groups of ≤7 items separated by 1 px separators spanning full width;
  ≤25 items per level; most-used first; destructive commands last.
  Submenus open to the right on hover with a right-pointing arrow glyph.
  <https://learn.microsoft.com/en-us/windows/win32/uxguide/cmd-menus>
- Row metrics: classic Win32 menu row ~23 px; modern WinUI/design-system
  rows 28-36 px (Radix/Carbon norm 36, compact 30-32); min panel width
  ~160 px, padding 4 px, 10-14 px left text indent (16 px icon gutter if
  icons present), separator = 1 px line + ~4 px vertical margins, submenu
  arrow at right edge, 4-6 px offset from the cursor/trigger.
- Canonical desktop context menu: Open | --- | Rename, Delete, Properties
  | --- | *shell items*: "Change wallpaper", "New folder/app",
  "Display settings". Right-click an empty region → shell menu;
  right-click an icon → item menu. Position: open downward-right of
  cursor, flip when it would clip the screen edge.

**Apply to HypeAI OS:**

- Add `kernel/win/deskicon.cc`: `Icon { char app[24]; hcpp::u8 col,
  row; }` cells on a `constexpr i32 cell = 96` grid inside the work area;
  paint between wallpaper (`desk_bg_paint_clip`, `deskman.cc:49-52`) and
  `wm_paint` — icons are *below* windows but *above* wallpaper, so they
  must be inside the clip region just like the windows pass.
- Seed icons from `hxrt_list()` (column-major, skip `HxerKind::System`
  entries — `hxrt_is_system` already exists); persist positions later to
  `HYPEOS://U/home/desktop.ini` via `hype::vfs::fs_write`.
- Icon rendering: reuse SpeedStart's tile painter
  (`speedstart.cc:119-150`) — centered monogram box + truncated label
  (`name_max = 11`, centered under icon). Selection: on left-press in an
  icon cell, draw `theme.accent`-alpha fill; double-click (two presses
  within ~400 ms / 40 pit ticks) → `desk_open_app`.
- Context menu: generalize the `WinMenu` widget into a shared
  `MenuWidget` in `kernel/ui/widgets/` (or keep in `win/`): items table
  of `{label, id}`, `menu_item_h` raised from 22 → 28 px, `menu_width`
  140 → 180, `menu_pad` stays 4 — the current `WinMenu` geometry
  (`winman.cc:28-31`) is already the right shape. Separators = sentinel
  item rendered as a 1 px `theme.border` line.
- Right button is free today: `route_buttons` (`hypeptr.cc:44-71`) only
  watches `ptr_button_left`; `ptr_button_right` (=2,
  `kernel/input/curstat.h:7`) rising edge in a desktop/icon region opens
  the desktop menu, inside a window opens that window's system menu —
  same `OverlayHost` path as `wm_menu_open` (`winman.cc:454-472`).
- "Change wallpaper" item → `desk_bg_reset()` / picker app
  (`deskbg.cc:109-112`); the image loading path already exists via
  `desk_bg_set_image` (PMM arena, bilinear cover-scale,
  `deskbg.cc:70-107`).
- Icon drag (defer): needs the same press-motion-release state machine as
  window drag (§3) writing back `{col, row}`; cheap because icons are
  inside damage anyway. v1: static grid + click/double-click only.

---

## 5. System tray / notification area / quick settings

**What exists**

- Notification area (Windows): right end of the taskbar — small 16 px
  status icons (volume, network, battery), overflow flyout for
  non-promoted icons, clock, "show desktop". Single-click icon = quick
  action/flyout, right-click = icon's context menu. Icon lifetime is
  owned by the app via `Shell_NotifyIcon`; icons must clean up on exit
  (dead-icon cleanup on hover is a known Windows wart).
  <https://learn.microsoft.com/en-us/windows/win32/uxguide/winenv-notification>
- Toasts: Windows balloons/toasts slide in from the notification corner
  (bottom-right), auto-dismiss, stack upward, click-through forbidden —
  click activates. freedesktop Notification Spec 1.3: a single
  session-scoped daemon owns stacking; each notification = {app_name,
  icon, summary, body, actions, hints, expire_timeout}; `expire_timeout`
  -1 = server default (~5 s), 0 = never expires; urgency levels
  {low, normal, critical} — **critical must not auto-expire**;
  `replaces_id` atomically updates a visible toast (progress reports).
  <https://specifications.freedesktop.org/notification/latest-single/>
- Quick settings (Win11/GNOME system menu): clicking the tray cluster
  opens a panel with toggles (Wi-Fi, airplane), sliders (volume,
  brightness), and a settings link — a popover above the tray, not a
  window.

**Apply to HypeAI OS:**

- Tray = right side of the taskbar band (§1): fixed cells [volume glyph,
  net glyph, clock]. State comes from what we already have —
  `hype::audio::hasio_ready()`/`pullmix` for a real volume level,
  `rtc_format` for the clock. No third-party tray protocol needed at v1:
  tray icons are shell-owned like GNOME's top bar.
- Toasts exist: `ToastHost` (`kernel/ui/overlays/toast.h`) already has
  `max_toasts = 8`, `ToastKind {Info, Success, Warning, Error}`,
  `default_duration_ticks = 300` ticks (~3-5 s at typical pit rate),
  title+message, stack painting in `toast.cc`. Missing pieces to match
  the spec: slide-in animation (animate `y` offset over ~8 ticks using
  `kernel/ui/anim.h`), a `Critical` kind that skips the countdown
  (`remaining` stays nonzero forever → no auto-expire), and
  `replaces_id`-style update (find by title match, overwrite message +
  reset `remaining`). All are additive in `Toast`; no new machinery.
- Toast damage: `wm_paint` paints overlays unconditionally inside the
  clip (`winman.cc:436`); toast ticks must mark damage each countdown
  frame — cheapest correct approach is `wm_invalidate(toast_region)` on
  show/tick/hide rather than `damage_full` (only the corner repaints).
- Quick settings: another `OverlayHost` panel anchored above the tray,
  built from existing widgets — `kernel/ui/widgets/switch.h` (toggles),
  `stepper.h` or `progress.h` rendered as a slider stand-in for volume,
  `button.h`, `separator.h`. Click outside or Escape closes, exactly the
  `WinMenu` dismiss semantics (`winman.cc:489-503`).
- Keep the one-modal limit in mind: `OverlayHost::open` holds a single
  `Widget* modal_` (`kernel/ui/overlays/overlay.h`) — taskbar menu,
  quick settings, context menus and start panel must *share* the slot
  (open-new-closes-old) rather than nest, which happens to match real
  shells (only one flyout open at a time).

---

## 6. Accessibility / power UX

**What exists**

- Lock: Win+L (Windows), Super+L (GNOME) — instantly covers the session
  with a lock screen showing clock + wallpaper; session state and open
  windows are preserved underneath; typing a password/PIN unlocks back
  into it. Auto-lock on idle timeout (~10-15 min default) is a policy,
  not shell mechanics.
- Shutdown/restart dialog: Windows uses Alt+F4-on-desktop ("Shut Down
  Windows" dialog) and the Start power flyout {Sleep, Shut down,
  Restart}; a confirmation step is required since keyboard focus can
  sit on the desktop. GNOME: system menu → Power Off dialog, 60 s
  countdown with "the system will power off automatically".
- User switching at login: Windows keeps sessions per user
  (Win+L → "Switch user" returns to the greeter without logging off);
  GNOME greeter lists users, new auth spawns a separate session.
- Accessible extras both shells share: on-screen visual feedback for
  Caps Lock, focus rectangles on keyboard navigation, Esc as the
  universal "cancel transient UI" key.

**Apply to HypeAI OS:**

- The session layer already models this: `SessionState` has `Booting →
  Greeter → Registering/Authenticating → LoggedIn`
  (`kernel/session/session.h:8-14`), the greeter paints via
  `kernel/ui/flowlogin.*`, and `session_handle_char` feeds auth.
  **Lock = a new state `Locked`** (or a `session_locked` flag) where
  `route_buttons` refuses input (`hypeptr.cc:54-57` already gates clicks
  on `session_logged_in()` — extend that gate to cover `Locked`) and the
  main loop repaints the greeter over the framebuffer without tearing
  down `slots[]`. Re-auth flips back to `LoggedIn`; windows and damage
  survive untouched.
- Keybinding: Win+L needs the LeftWin/LeftCtrl+char mapping in
  `kernel/istatekeys.h` + `scancode_key_pressed`
  (`kernel/input/scancode.h`); simplest v1 is Ctrl+Alt+L or F-key since
  Super key handling varies by scancode set. Idle lock: count
  `pit_wait_ticks` iterations in the main loop (`main.cc:928-948`) with
  no `cursor_poll`/`keyring` activity → set `Locked`.
- Shutdown dialog: an `OverlayHost` modal (pattern in `winman.cc:160-205`)
  with two `button.h` buttons {Power off, Restart} — QEMU power-off is
  `outw(0x604, 0x2000)` (Bochs/QEMU ACPI shutdown port) or
  `outw(0xB004, 0x2000)` for older PIIX; reboot is `outb(0x64, 0xFE)`
  (keyboard controller reset) — add a tiny `kernel/power.cc` rather than
  going through full ACPI S5. Fallback `halt_forever()` exists at
  `main.cc:83`.
- User switching: `session_user()`/`session_register`
  (`kernel/session/session.h:19-24`) already support multiple users;
  "Switch user" from the lock screen just re-enters `Greeter` while
  keeping the previous session parked — defer parking *per-user* window
  sets until multi-user matters; v1 lock is single-user.
- Esc-always-cancels is already the overlay convention
  (`wm_handle_key` → `overlays.handle_key`, `winman.cc:526-536`) — keep
  it uniform for lock, shutdown, context menus, start panel.

---

## 7. Shell rendering / hit-testing notes (toolkit level)

**How real shells draw the chrome**

- Windows: taskbar + start menu are ordinary topmost windows owned by
  the shell (`Shell_TrayWnd`, `DV2ControlHost`); notification toasts are
  popup topmost windows in a separate z-band above apps; menus are
  `TPM_*` popup windows. Z-order is *banded*: desktop icons layer <
  normal windows < topmost windows < tooltips/menus.
- GNOME: the compositor (mutter) owns the stage; shell UI (top bar,
  overview, OSD, notifications) lives in `uiGroup`/`layoutManager`
  chrome actors painted *by the compositor itself*, always above client
  windows, with `trackChrome` regions excluded from window placement.
  Damage is per-clip-region; the panel is just geometry in the same
  paint pass — exactly our model minus surfaces.
- OSDev guidance: a layered (non-compositing) WM redraws by computing
  changed regions and blitting only the visible parts; the classic
  failure mode is expose-repaint on every uncover, which damage tracking
  avoids only if windows repaint from their own model — which is what we
  do, since `hxrt_exec` re-renders app content every paint call anyway.
  <https://wiki.osdev.org/Compositing>
  <https://wiki.osdev.org/Graphics_stack>

**Input hit-testing order**

Universal order in every stacking shell (highest first):

1. Pointer cursor sprite (never hit-testable itself)
2. Menus / popups / toasts (transient top band — capture until dismissed)
3. Taskbar / dock / top bar (always-on-top band)
4. Topmost windows → normal windows, top-down by z
5. Desktop icons
6. Wallpaper (right-click target for shell menu)

**Apply to HypeAI OS:**

- Our paint pipeline already *is* banded; make it explicit as a comment
  contract in `desk_paint` (`deskman.cc:38-53`):
  `wallpaper → desk icons → wm_paint(windows ascending z) → taskbar →
  overlays (menus + toasts) → cursor sprite`. `cursor_draw` is already
  last in the loop (`main.cc:929-936`) and `cursprite_erase` precedes
  repaint — keep that ordering when adding bands.
- Hit-test: mirror the paint order in reverse inside `wm_handle_mouse`
  (`winman.cc:484-520`) — `OverlayHost` first (already true via
  `wm_menu_visible`), then taskbar, then the descending-z window loop,
  then desk icons, then wallpaper. One dispatcher, same order table.
- Damage is a single union rect today (`damage_add`, `winman.cc:50-56`).
  With a taskbar + icons + toasts the union will often cover half the
  screen even for small changes; acceptable at 1024x768 software render
  but plan `damage[]` small-array (≤8 rects) before adding per-minute
  clock invalidation + animation, or restrict animated regions to fixed
  small rects so union stays tight.
- Hover: `route_buttons` only fires on button transitions; hover states
  (taskbar hot tracking, icon hover, menu highlight-follows-mouse) need
  a position-changed notification — `ptr_take_dirty`/`cursor_poll`
  returns dirty on motion (`hypeptr.cc:96-112`), so feed the last
  `CurStat` position into the shell's hit zones each poll and
  `wm_invalidate(hover_rect)` on state change only.
- No per-window buffers ⇒ no live thumbnails for Alt-Tab/overview —
  use icon+label lists (Win3.1 switcher). Conversely, every repaint is
  "from scratch" anyway, so minimizing/unminimizing is zero-cost:
  nothing to snapshot, `hxrt_exec` re-renders on next paint
  (`winman.cc:418-428`).

---

## Build order (recommended)

1. Taskbar band (Start button + running buttons + RTC clock) + work-area
   reservation. → `kernel/win/taskbar.cc`, touches `deskman.cc`,
   `winman.cc` hit-test + cascade region.
2. `WinState::{Minimized, Maximized}` + restore rect + title-bar menu
   extension. → `winman.h/.cc`.
3. Keyboard dispatch into WM (scancode → DecodedKey → `wm_handle_key`
   with modifiers) — unlocks Alt-Tab, Esc, Start-menu typing, Win+L.
   → `kernel/main.cc` loop + `kernel/input/scancode.h`.
4. Start panel (overlay-mode SpeedStart). → `kernel/hxrt/apps/speedstart.cc`,
   `OverlayHost`.
5. Window drag + snap zones + preview rect. → `winman.cc`, `hypeptr.cc`.
6. Desktop icons + right-click context menus. → new `deskicon.cc`,
   shared MenuWidget, `hypeptr.cc` right-button edge.
7. Toast upgrades (slide-in, critical persistence, replaces) + quick
   settings panel. → `kernel/ui/overlays/toast.cc`, widgets.
8. Lock state + power dialog. → `kernel/session/session.*`,
   `kernel/power.cc` (outb 0x604 / 0x64).
9. Damage rect list (≤8) + virtual-desktop field, if profiling demands.

Each step is independently testable via the existing pattern:
`wm_selftest` (`winman.cc:559-611`), `ui::selftest` widget probes
(`kernel/ui/selftest.cc`), and serial log assertions.
