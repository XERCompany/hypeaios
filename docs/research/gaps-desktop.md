# Gaps audit — missing desktop/userland features vs Windows / Ubuntu

Branch `devin/p20-gapsdesk`. This document inventories the userland and
desktop-shell features that HypeAI OS still lacks relative to Windows 10/11
and Ubuntu (GNOME), and maps each gap to a concrete hook point in our code.

**Already implemented (out of scope):** taskbar + Start menu + tray + clock
(`kernel/ui/deskbar.cc`), desktop icons + right-click menus
(`kernel/win/deskicons.cc`), window min/max/restore + Aero Snap + Alt-Tab
(`kernel/win/winman.cc`), quick-settings panel + toasts
(`deskbar.cc`, `kernel/ui/overlays/toast.cc`), lock screen (Win+L) + power
dialog (`kernel/session/lockscr.cc`, `session.cc`), dark theme
(`kernel/ui/theme.cc`), white cursor (`kernel/input/cursprite.cc`), XAU
windowing API (`kernel/win/xau.cc`), XDP render manager (`kernel/xdp/`),
RevServices + RevDBGService (`kernel/svc/revsvc.cc`), .hxer/.hxersys apps
(`kernel/hxrt/apps/`), login/greeter session (`kernel/session/session.cc`),
FAT32 + memfs + fd table (`kernel/vfs/`), wallpaper cycler
(`kernel/win/deskbg.cc`).

Reference sources cited throughout: Microsoft Windows UX guidelines /
Win32 shell docs, GNOME Human Interface Guidelines (HIG), freedesktop.org
specs (XDG clipboard, drag-and-drop, notifications, thumbnail spec, XKB),
and osdev.org notes (HID usage tables, PS/2 set-1, EDID).

Each feature entry gives: **Reference** — the mainstream behavior we are
measured against; **UX** — what the user expects; **Data** — state we must
keep; **Hook** — the actual file(s) where the work lands; **Effort** — a
rough size (S < 300 LOC, M ~300–800, L > 800 or cross-cutting).

---

## 1. Clipboard (Ctrl+C/X/V, clipboard ring, cross-app types)

**Reference.** Windows `user32` clipboard (`SetClipboardData`, `CF_TEXT`
et al., clipboard history Win+V ring of ~25 in Win10+); GTK `GdkClipboard`
mime offers; X11 CLIPBOARD/PRIMARY selections via ICCCM; Wayland
`wl_data_device`. No freedesktop *spec* — it's toolkit/IPC convention.

**UX.** Ctrl+C copies in one app, Ctrl+V pastes in another; Ctrl+X cuts;
Win+V shows recent clips. Image/file-list payloads are a stretch goal.

**Data.** A clipboard store: fixed slots {mime type, byte blob}; a ring of
N=8 history entries; an owner/sequence counter so apps can detect changes.
Persist nothing (Windows clears on reboot; keep it that way).

**Hook.** New `kernel/win/clipboard.cc` service (or `kernel/ui/clipman.cc`).
Wire Ctrl+C/X/V chords into `wm_handle_key` (`kernel/win/winman.cc:1572`)
or better into XAU so every app inherits it: add
`XAU_MSG_CLIPBOARD_*` / a `xau_clipboard_set/get` pair in
`kernel/win/xau.cc`; the scancode layer already delivers Ctrl+key chords —
`scancode_key_pressed(key_ctrl)` + ASCII `\x03`/`\x18`/`\x16` in
`kernel/input/scancode.cc`. TextInput widget (`kernel/ui/widgets/textinput.cc`)
is the first consumer.

**Effort.** M.

## 2. Drag & drop

**Reference.** Windows OLE `IDropSource`/`IDropTarget` + `DragDrop` cursors;
GNOME GTK `GtkDragSource`/`GtkDropTarget` with mime offers; freedesktop
XDND protocol (X11) / `wl_data_offer` (Wayland). Common cases: drag file
icon onto an app window, drag window/task-button to pin, drag desktop icon
to rearrange.

**UX.** Press+move past a threshold starts a drag: cursor changes, a drag
image follows the pointer, drop targets highlight, release commits. Escape
cancels.

**Data.** A drag session struct {payload kind (icon id, path, win id),
source, current target}; desktop icon grid positions need a persistent
`{col,row}` per icon (spec §5 already sketches writing icon order back —
`docs/research/desktop-shell.md:310`); pin list for taskbar (see §9).

**Hook.** `wm_handle_move`/`wm_handle_mouse` in `kernel/win/winman.cc`
(the titlebar drag engine at `winman.cc:84-88, 623-631, 1401` is the
template — reuse its press-threshold + `drag_engaged` pattern). Desktop
rearrange: `desk_icons_handle_click` (`kernel/win/deskicons.cc`) gains a
press+move branch writing `Icon.pos`; drop-onto-window hit-tests
`wm_window_at` frames and posts a new `XAU_MSG_DROP` via `xau_post`
(`xau.h:111`); taskbar pin drag lands in `deskbar_handle_mouse`
(`deskbar.cc:850`).

**Effort.** L (icon rearrange alone is M).

## 3. File manager app

**Reference.** Windows Explorer (navigation pane, list/icon views, file
ops, status bar); GNOME Files/Nautilus (breadcrumb bar, grid view,
GNOME HIG recommends `Ctrl+L` location bar, `F2` rename, `Ctrl+C/X/V`
file ops, trash instead of delete).

**UX.** Browse `HYPEOS://U` and `HYPEOS://F`; double-click to open,
right-click for copy/move/delete/rename/new-folder; breadcrumb or path
field; list view with name/size columns; thumbnails/previews for text and
image payloads.

**Data.** Current path, selection set, per-view scroll offset, a copy/cut
file-list clipboard (same store as §1 with mime `x-hype/filelist`), file
metadata from `FsStat {name, flags, size, disk}` (`kernel/vfs/hypefs.h:33`).

**Hook.** New app `kernel/hxrt/apps/hxfiles.cc` registered via
`builtins.cc` and installed next to the others in
`kernel/main.cc:487-499`. Reads directories with `fs_list`, mutates with
`fs_mkdir/fs_write/fs_delete` (`kernel/vfs/hypefs.h:61-66`); copy needs a
read+write loop (no `fs_copy` exists — add one in `hypefs.cc` or compose
from `fs_open`/`fs_pread`/`fs_pwrite` in `kernel/vfs/fdtable.cc`). UI from
existing widgets: `Panel`, `Flex`, `ListBox`, `Table`, `TextInput`,
`Alert` for confirm-delete (`kernel/ui/widgets/`). Needs `fs_rename`
(new, ~50 LOC inside `hypefs.cc` dir-entry edit).

**Effort.** L — the biggest single app, but all primitives exist.

## 4. Terminal / console app (hxshell)

**Reference.** Windows Terminal/CMD, GNOME Terminal/bash; osdev "writing a
shell" notes. A REPL over the OS API surface is the classic missing piece.

**UX.** `hxshell` window with a prompt, line editing (history via Up/Down),
builtin commands: `ls cd pwd cat echo mkdir rm cp mv`, `apps`, `launch
<name>`, `svc`, `net`, `vol`, `mem`, `theme dark|light`, `help`. Pipes are
optional; exit code echo is cheap and satisfying.

**Data.** Line buffer, history ring (16), cwd path (default `HYPEOS://U/`),
per-command dispatch table.

**Hook.** New `kernel/hxrt/apps/hxshell.cc` as a .hxer app. Needs a real
text-editing widget inside the window — `TextInput` is single-line, so
either compose with `ListBox` (output scrollback) + `TextInput` (prompt),
which is much easier than a full VT100 surface. Input arrives through
`XAU_MSG_KEYDOWN`/`XAU_MSG_CHAR` (`kernel/win/xau.h:50-52`). Commands call
`fs_list/fs_read/fs_mkdir/fs_delete` (`kernel/vfs/hypefs.h`),
`hxrt_list/desk_open_app`, `hype::net::net_ip` (`kernel/net/net.h`),
`pullmix_set_master_gain` (`kernel/audio/pullmix.h`),
`theme_set` (`kernel/ui/theme.h`).

**Effort.** M.

## 5. Text editor app (notepad)

**Reference.** Windows Notepad (open/save, find, status bar, word-wrap);
GNOME Text Editor (HIG: header bar, open/save dialogs, unsaved-dot).

**UX.** Open a .txt from the file manager or a launch arg; edit with
cursor keys/backspace/enter; Ctrl+S save, title shows `name — modified`.

**Data.** Gap buffer or plain line array (cap ~64 KB), cursor {line,col},
scroll, dirty flag, bound fd/path.

**Hook.** New `kernel/hxrt/apps/hxedit.cc`. File I/O via the fd table —
`fs_open(FS_OPEN_READ|FS_OPEN_CREATE)`, `fs_pread/fs_fwrite`, `fs_seek`,
`fs_flush` (`kernel/vfs/fdtable.h:33-55`). Rendering via
`draw_text_sdf`/hypetext (`kernel/font/hypetext.cc`) or the widget stack.
The hard part is a multi-line editable surface: nothing in
`kernel/ui/widgets/` does multi-line editing today (TextInput is
single-line, field.cc is form-scoped) — that widget (`textarea.cc`) is the
bulk of the work and is also needed by hxshell scrollback and the help
browser.

**Effort.** M (L if we do gap buffer + proper caret navigation).

## 6. Settings app depth

**Reference.** Windows Settings (Personalization > Colors/Themes, Time &
Language > Date & time/Region, System > Display, Bluetooth & devices >
Typing); GNOME Settings panels (Appearance, Displays, Region & Language,
Keyboard, Sound).

**UX.** HxSettings becomes a real app with sidebar tabs:
- *Theme*: dark/light/high-contrast picker + accent color swatches.
- *Display*: resolution/mode list, brightness (QS slider is a stub —
  `deskbar.cc:78` `qs_bright` is visual-only).
- *Time & language*: timezone offset from RTC, 12/24h, date format,
  locale pick (affects formats, §13).
- *Keyboard*: layout RU/EN (§13), sticky-keys toggles (§12).
- *Sound*: master/output sliders wired to pullmix, per-channel if the
  mixer exposes it.

**Data.** A persisted settings file, e.g. `HYPEOS://U/settings.ini` —
flat `key=value`, loaded at session begin; this is the first real use of
`fs_read/fs_write` for shell state.

**Hook.** `kernel/hxrt/apps/hxsettings.cc` (59 lines today — placeholder
text "settings placeholder" at line 34). Tabs widget exists
(`kernel/ui/widgets/tabs.cc`), as do `Switch`, `RadioGroup`, `Stepper`,
`ListBox`. Backends: `theme_set/theme_dark/theme_light`
(`kernel/ui/theme.h:49-52`), `rtc_now` + a new `rtc_set`
(`kernel/time/rtc.cc`), `pullmix_set_master_gain`
(`kernel/audio/pullmix.h`), display mode via XDP/backend re-init
(`kernel/xdp/xdp.cc`).

**Effort.** M.

## 7. Notification center (persistent toast history)

**Reference.** Windows Action Center / Win11 notification flyout (Win+N):
stacked notifications grouped by app, clear-all, persists until dismissed.
GNOME: clock-click notification list with per-app clear.

**UX.** Clicking the tray bell opens a panel above the taskbar listing the
last N notifications with time stamps; a "Clear all" button; bell badge
already shows unread count (`deskbar.cc:89,494-502`).

**Data.** We already ring-buffer `ToastText{title,msg}` × max_toasts
(`deskbar.cc:82-88`); extend to a history array of ~16 entries with an
`RtcTime` stamp each, independent of the live toast lifetime.

**Hook.** `kernel/ui/deskbar.cc`: `deskbar_notify` (line 953) appends to
the history ring; `bell_zone()` click (line 916) currently only clears the
badge — repurpose to open a flyout modeled directly on `paint_qs` /
`qs_rect_now` slide-in machinery (lines 150-169, 616). Toast rendering in
`kernel/ui/overlays/toast.cc` supplies the card look.

**Effort.** S–M. High value/effort ratio.

## 8. Search in Start (type-to-filter apps + files)

**Reference.** Windows 10/11 Start: typing with the menu open filters apps
and files, first result Enter-launches. GNOME Shell Activities: type →
app grid filters + file results.

**UX.** Start menu gains a search field on top; any keystroke while open
goes to it; matching app names (substring, case-insensitive — `name_less`
at `deskbar.cc:255` is the pattern) and U:/ file leaf names via `fs_list`;
arrow keys + Enter or click to activate.

**Data.** Query string (≤32 chars), filtered `menu_order` list, optional
file result list (cap 8 `FsStat`).

**Hook.** `kernel/ui/deskbar.cc`: `build_menu_order` (line 268) gains a
filter arg; `open_start` (314) resets the query; keystrokes arrive via
`wm_handle_key` → a new `deskbar_handle_char` while `start_open`. File
hits walk `fs_list("HYPEOS://U/...")` from `kernel/vfs/hypefs.cc` — a
recursive walk of U:/ roots is bounded and fine for now.

**Effort.** M. Great demo payoff.

## 9. Pinning to taskbar + jump lists

**Reference.** Windows taskbar pinned apps live left of task buttons,
launch-or-focus on click, right-click shows a jump list (recent/pinned
files, tasks). GNOME dash favorites similarly persist across runs.

**UX.** "Pin to taskbar" on a Start-menu or desktop-icon context item;
pinned icons stay in a dedicated strip; right-click on a task button opens
a jump list with recent files (once §3/§5 record them) + Close/Unpin.

**Data.** Pin list: app names, ≤8, persisted in the settings file (§6);
jump-list recents: per-app small ring of paths (cap 4), same file.

**Hook.** `kernel/ui/deskbar.cc` — a pin strip between `start_btn` and
task buttons; context menu via the existing `wm_ctx_open` mechanism
(`kernel/win/winman.h:80`, already used by deskicons); persistence via
settings file on `U:/`. Launch = `desk_open_app` (`deskman.cc:100`).

**Effort.** M (S for pins alone; jump lists need the recents plumbing).

## 10. Virtual desktops / workspaces

**Reference.** Win10 Task View (Win+Tab, Win+Ctrl+D, Win+Ctrl+Left/Right);
GNOME dynamic workspaces (vertical, always one empty trailing).

**UX.** Our own spec already fixes the data model
(`docs/research/desktop-shell.md:201-209`): a `desktop` field per `Win`
and a current-desktop index, filtered in window collection. Win+Ctrl+←/→
switches; Alt-Tab and taskbar show only current-desktop windows;
a small workspace pager in the tray.

**Data.** `Win::desktop` (u8), `cur_desktop` in winman, optional pager
rects in the bar.

**Hook.** `kernel/win/winman.h:28-42` add `desktop` to `struct Win`;
`collect_mru` (`winman.cc:689`) and the deskbar task-button loop
(`deskbar.cc:820-830`) filter on it; new `wm_set_desktop(id)` +
`wm_switch_desktop(delta)` invalidate all. Chords: extend the key path in
`wm_handle_key`/lockscr chord table (`lockscr.cc:22-31` pattern —
key_lwin is already decoded).

**Effort.** M. Spec already done; it's mostly filtering.

## 11. Taskbar window thumbnails on hover

**Reference.** Windows 7+ taskbar live thumbnails; GNOME has none natively
(extension territory).

**UX.** Hover a task button ~500 ms → a small live preview card pops above
the button: scaled copy of the window contents + title; click preview to
focus, X on preview to close.

**Data.** Per-window thumbnail bitmap. We *can* grab content cheaply:
re-run the app's paint into a small offscreen `Framebuffer` (XAU paint
hook `WmXauPaintHook`, `winman.h:107`) or downsample the compositor back
buffer region (`deskman.cc:39 back_fb` / XDP surface view) — the latter is
free because the back buffer already holds the composed desktop at
scanout format.

**Hook.** `kernel/ui/deskbar.cc` hover tracking (add pointer-move aware
task-button hover; `deskbar_idle` ticks already run per frame);
capture path in `kernel/win/deskman.cc` blit_clip-adjacent code — copy
`Win::frame` region of `back_fb`/`xdp_view` scaled into a thumbnail slot,
paint above bar. Hover timing via `time/timer.h` ticks.

**Effort.** M. Showpiece feature; the compositor already did the hard part.

## 12. Accessibility

**Reference.** Windows Ease of Access (Narrator, Magnifier Win+=,
high-contrast themes, Sticky Keys 5×Shift, Filter Keys, on-screen
keyboard); GNOME Settings > Accessibility; Linux a11y runs through AT-SPI
(no freedesktop *render* spec — it's app-side); WCAG contrast ratios.

**UX (scoped to our shell).**
- *High-contrast theme*: a third `Theme` with ≥7:1 text contrast.
- *Sticky keys*: latch Ctrl/Alt/Shift/Win on discrete presses — pure
  scancode-layer work, huge for one-finger users.
- *Magnifier*: 2× lens following the cursor (copy-zoom of the back-buffer
  region — same trick as §11).
- *On-screen keyboard*: bottom panel emitting `XAU_MSG_CHAR` to the
  focused window (also useful for tablet demos).
- *Screen reader hooks*: deferred — emit a serial "a11y announce" marker
  stream (focus changes, menu open, toast text) a TTS backend could
  consume later; cheap, and doubles as a testability hook.

**Data.** A11y settings struct (persisted, §6); sticky-latch bitmask;
zoom factor; OSK flag.

**Hook.** Theme: `kernel/ui/theme.cc` (add `theme_contrast()`; `theme_set`
exists). Sticky keys: `kernel/input/scancode.cc` — `scancode_key_pressed`
already tracks held state (`scancode.h:18`), add latching before decode.
Magnifier: `deskman.cc` post-blit pass or a `kernel/ui/overlays/` card.
OSK: `kernel/hxrt/apps/` window posting chars via `wm_handle_key`.
Announce stream: `serial::write_line` hooks in `wm_focus`,
`deskbar_notify`.

**Effort.** M total (each item S–M); announce markers are S.

## 13. i18n / IME: keyboard layouts, UTF-8 path, locale formats

**Reference.** Windows IME/keyboard layouts (Win+Space cycles; RU/EN is
the canonical two-layout case); freedesktop XKB (xkeyboard-config layout
tables, ISO_Level3 shift); GNOME Region & Language. UTF-8 is the wire
format everywhere modern.

**UX.** Win+Space or Alt+Shift flips EN↔RU; the tray shows the active
layout ("EN"); typing produces Cyrillic. Locale affects date/time/number
format in clock, calendar, file sizes.

**Data.** Layout table: scancode→codepoint map per layout (set-1 already
decoded, `kernel/input/scancode.cc` — today it emits ASCII only, RU needs
a second table emitting UTF-8 or UTF-32 for `-`); active-layout index
persisted in settings; locale struct {decimal sep, date order, 12/24h,
first-day-of-week}.

**Hook.** `kernel/input/scancode.cc` — `scancode_decode` gains a layout
parameter; the ASCII channel widens to a codepoint (XAU `XAU_MSG_CHAR`
lparam already 64-bit, `xau.h:52`). Text path: `hcpp::utf8::next_codepoint`
is already used by `hypetext.cc:105,150` and `ttf.cc` resolves
`glyph_index(codepoint)` (`kernel/font/ttf.h:30`) — **check Montserrat
coverage**: `montserrat_ttf_data.cc` — Cyrillic subset may be absent;
if so, bundle a second font or the raster fallback shows tofu. Tray glyph
+ toggle chord in `deskbar.cc`. Locale formatting: central
`fmt_date/fmt_num` helpers used by clock (`deskbar.cc:512-531`), lock
screen (`lockscr.cc:93-99`), calendar (§19).

**Effort.** M (font coverage is the wildcard).

## 14. Media keys (play/pause/volume via HID consumer page)

**Reference.** USB HID Consumer Page usage 0xB0 play, 0xB7 stop, 0xCD
play/pause, 0xE9 vol+, 0xEA vol−, 0xE2 mute; Windows maps them to
`VK_MEDIA_*`; GNOME Shell grabs them via gnome-settings-daemon.

**UX.** Vol+/− keys move the QS volume slider (with on-screen volume HUD —
a toast-like transient bar); mute toggles speaker glyph; play/pause posts
to a media app when one exists.

**Data.** Consumer-usage → action map; no persistence.

**Hook.** `kernel/usb/xhci.cc:543-635` — the HID path today handles only
the boot-protocol keyboard report with `hid_map[0x66]`; consumer-page
devices need either a second interface parser or the QEMU/tablet PS/2
path synthesizing extended scancodes (vol keys arrive as `0xE0` prefixed
set-1: vol− 0xE02E, vol+ 0xE030, mute 0xE020). Shortest path: extend
`scancode_decode` for the E0 multimedia scancodes → new `XAU_MSG_MEDIA`
or direct `pullmix_set_master_gain` calls + a HUD toast via
`deskbar_notify`-adjacent transient card.

**Effort.** S–M depending on whether we fake it via PS/2 scancodes (S) or
parse HID consumer reports (M).

## 15. Multi-monitor (extend/duplicate)

**Reference.** Windows display settings: Duplicate / Extend / Show-only;
EDID per output; GNOME Displays panel with join/mirror. osdev EDID notes.

**UX.** Second display shows the same desktop (duplicate) or extends the
work area; windows can be dragged across; taskbar on primary only
(Win default) with optional mirror.

**Data.** Output list {backend, mode, origin, role}; virtual desktop =
union of output rects; per-output damage.

**Hook.** `kernel/xdp/` is the right layer — `xdp_device[]` already
enumerates display devices with `backend` binding (`xdp.h:144-153`);
`backend_virtgpu.cc` can create a second scanout resource; `xdp_present`
already fans damage to "every present-capable backend"
(`deskman.cc:124-138`). Duplicate = present the same surface to 2
backends (small). Extend = a `screen` rect spanning outputs +
`ptr_set_bounds` to the union (`kernel/input/ptrmv.h:9`) + work-area math
on the primary. VBE single-scanout (`backend_sw.cc`) can't extend — QEMU
virtio-gpu multi-head or Bochs dispi only.

**Effort.** L for extend, S for duplicate (once a second backend exists).

## 16. User accounts depth

**Reference.** Windows multi-user (Settings > Accounts > Family), Guest,
auto-login registry keys, per-user profile dirs; GNOME Settings > Users
with autologin toggle. Password storage: salted hash never plaintext
(Windows uses NTLM/SAM; Linux `/etc/shadow` bcrypt/yescrypt).

**UX.** Greeter lists all users; "Switch user" from the lock screen and QS;
per-user `U:/home/<name>` data root; optional autologin for the demo VM;
a guest account with no persistence.

**Data.** Today: `session_register` + `hello::hello_auth` PIN path
(`session.cc:163-186`), `userservice.cc` capabilities, `hellostore.cc`
credential blobs, scripted `demo` user (`session.cc:31-32`). Missing: a
user roster for the greeter, per-user `U:/home/<name>` roots, an
autologin flag, guest (in-RAM user, wiped U: subdir), and fast
user-switch (per-session window sets — nearly free once §10's
`Win::desktop` exists: give each user a desktop).

**Hook.** `session.cc` greeter lists users via a new `users_enumerate`
(`kernel/userservice.cc`); registration persists via `fs_write`
(`hellostore.cc` already has blob persistence); autologin flag read at
`session_begin`. Fast-switch = `session_lock` + greeter pick with
windows kept alive via the `Win::desktop` tagging.

**Effort.** M.

## 17. Screensaver / idle dimming → auto-lock

**Reference.** Windows Screen saver + "On resume, display logon screen";
GNOME blank-screen delay + automatic screen lock. Dimming precedes blank
on Ubuntu by ~30 s.

**UX.** After N idle minutes (no key/pointer events), screen dims →
screensaver animation (starfield/blank) → lock veil requiring unlock.
Any input wakes instantly.

**Data.** Idle timer = now − last_input_tick (reset in the input path);
timeout setting (persisted); saver kind enum.

**Hook.** `kernel/main.cc` main loop already branches on
`session_locked` (line 1049): add an `Idle` tier — record last-input
tick where `wm_handle_key`/`wm_handle_mouse` are dispatched; on timeout
call `session_lock()` (`session.cc:309`) and before that run a saver
paint pass (new `kernel/ui/screensaver.cc` or reuse lockscr veil with a
starfield — `lockscr.cc` paint machinery is a fine template). Dimming =
alpha fill pass, trivial.

**Effort.** S.

## 18. Help system

**Reference.** Windows F1 → Help/Support app; GNOME Yelp (Mallard
markdown-ish); freedesktop has no help spec.

**UX.** F1 in any window opens "HxHelp" with a sidebar of topics; content
is markdown rendered from `docs/wiki/*.md` — we literally ship a wiki in
the tree (`docs/wiki/index.md`, `hypeai-font-ui.md`, `core-os-p*.md`,
11 files).

**Data.** A minimal markdown subset renderer: `#`→heading, `-`→bullet,
backtick code spans, blank-line paragraphs. Text-only, monospace ok.

**Hook.** New `kernel/hxrt/apps/hxhelp.cc`; wiki pages copied into
`U:/docs/` at seed time (`fs_seed`, `kernel/vfs/hypefs.cc`) or embedded
as blobs like `montserrat_ttf_data.cc`; F1 chord in `wm_handle_key`
opens `desk_open_app("HxHelp")`; needs the multi-line text surface from
§5 (read-only mode).

**Effort.** M (S once the read-only text view exists).

## 19. Clock / calendar flyout

**Reference.** Win10/11 tray-clock flyout: month calendar + full date +
agenda space; GNOME clock-click calendar+notifications combo.

**UX.** Click "HH:MM" in the tray → panel with a month grid (today
highlighted), full date line, and (once §7 lands) the notification list
beneath — exactly the GNOME layout.

**Data.** RTC gives `{year, month, day, hour, minute}` via
`hype::time::rtc_now` (`kernel/time/rtc.h`, already used at
`deskbar.cc:750-765`); month-grid math (day-of-week of day 1, month
lengths, leap rule ~30 lines).

**Hook.** `kernel/ui/deskbar.cc` — `clock_rect()` click (currently a
consume-only no-op, line 924-925) opens a third flyout sharing the
`qs_rect_now` slide-in pattern; paint uses `paint_label` + `fmt_u32`.

**Effort.** S.

## 20. Window open/close/minimize animations

**Reference.** Windows: window genie-minimize to taskbar, fade-in dialogs;
GNOME mutter: open = scale+fade from center, minimize = slide to dock,
close = shrink+fade. ~200-250 ms, ease-out.

**UX.** Windows ease open (scale 0.9→1 + fade), minimize slides toward
the task button, close shrinks away. Snappy but smooth — the polish gap
between "functional shell" and "feels like a real OS".

**Data.** Per-window animation record {kind, t0, duration, from-frame,
to-frame}; damage of union rect each tick.

**Hook.** `kernel/ui/anim.cc` `AnimTicker` (16-widget cap — the widget
version) is the tick source; window transforms happen in `wm_paint`'s
per-window loop in `kernel/win/winman.cc` — simplest honest version is
rect interpolation of `Win::frame` (slide + shrink), since true content
scaling needs the §11 thumbnail path. Frame-rect morph for min/restore is
straightforward damage unioning; fade-in of *content* requires the
offscreen surface — defer that part.

**Effort.** M.

## 21. Context-menu editor / file associations

**Reference.** Windows `assoc`/`.txt → notepad` registry hive +
OpenWithList; freedesktop `mimeapps.list` + shared-mime-info; GNOME
"Open With Other Application".

**UX.** Double-clicking `readme.txt` on the desktop launches HxEdit;
right-click "Open with…" offers registered apps; `.hxer` launches,
`.md` opens HxHelp, `.bmp/.png` a viewer, `.wav` the audio player.

**Data.** Extension → app-name table (defaults compiled in, user
overrides in settings file); desktop-icon entries need a "kind: file vs
app" flag — `deskicons.cc` already seeds icons from both `hxrt_list`
apps and U:/ root files (`deskicons.h:16-18`).

**Hook.** `kernel/win/deskicons.cc` — `desk_icons_handle_click`
double-press path currently launches apps only; route files through a new
`assoc_for(name)` (small table in `kernel/win/assoc.cc` or inside
deskicons) → `desk_open_app` + launch-arg path passed via the app ctx.
"Open with" item in the icon ctx menu (`wm_ctx_open` items).

**Effort.** S–M.

## 22. Package / installer UI

**Reference.** Windows .msi/MSIX + Settings > Apps; GNOME Software +
.flatpak; freedesktop AppStream metadata.

**UX.** A `.hxer` blob dropped on the desktop or double-clicked in the
file manager → "Install app?" dialog → lands in `U:/apps`, appears in
Start, icons refresh; an "Apps" settings pane lists installed apps with
Remove.

**Data.** Install = what `hypefile_install` already does
(`kernel/vfs/hypefile.cc:98-143`: hxpack encode → `fs_write` →
`hypeblob_sync`); load path `hypefile_load`/`hypefile_sync` exists
(lines 145-215). Missing: a UI front-end + an uninstall path
(`fs_delete` on the .hxer + `hxrt_unregister`).

**Hook.** Dialog = `kernel/ui/widgets/dialog.cc` + `alert.cc`; trigger =
assoc rule from §21 (`.hxer` → installer flow rather than direct exec);
list pane in `hxsettings.cc` calling `hxrt_list` (`kernel/hxrt/hxrt.h`);
after install/uninstall call `desk_icons_refresh` (`deskicons.h:19`) +
rebuild Start order (`deskbar.cc:268`).

**Effort.** S–M — machinery is all there; it's glue + dialogs.

## 23. Screenshot tool (PrtScr → save to U:/shots)

**Reference.** Windows PrtScr (fullscreen to clipboard), Win+PrtScr
(saves PNG to Pictures), Win+Shift+S (region Snipping Tool); GNOME
interactive screenshot UI (PrtScr → area/window/screen picker).

**UX.** PrtScr grabs the compositor back buffer, encodes, saves
`U:/shots/shotNNN.bmp` (BMP = zero-dep encoder; PNG needs a deflate —
`hypeblob`/`hxpack` may already carry zlib inflate only); toast
"Screenshot saved"; optional clipboard copy (§1, image mime).

**Data.** Encoder (24-bit BMP ~30 lines), shot counter, save dir.

**Hook.** `kernel/win/deskman.cc` — `back_fb`/`xdp_view` holds the full
composed frame at scanout format (`deskman.cc:39-96`); capture = memcpy +
encode + `fs_write`. PrtScr = set-1 scancode `0xE037` — hook in the same
chord table pattern as Win+L (`lockscr.cc:22-31`).

**Effort.** S.

## 24. Recycle bin

**Reference.** Windows `$Recycle.Bin` with per-volume hidden folder,
restore/empty, confirm-delete prompt; GNOME `~/.local/share/Trash/files`
+ `/info` sidecars per freedesktop Trash spec.

**UX.** Delete moves the file to `U:/.trash/` (+ a `.info` sidecar with
origin path + delete time); a trash desktop icon offers Empty/Restore via
context menu; permanent delete via Shift+Del.

**Data.** Trash dir entries, sidecar text `path=<orig>\ntime=<rtc>`;
restore needs the origin so sidecars are mandatory, not optional.

**Hook.** `kernel/vfs/hypefs.cc` — safest approach keeps `fs_delete`
semantics and adds `fs_trash(path)` in a new `kernel/vfs/hyptash.cc`
wrapper (rename into `.trash`, write sidecar); callers: file manager (§3),
deskicon ctx Delete (`deskicons.cc`), hxshell `rm`. Empty = `fs_list` +
`fs_delete` loop; restore = read sidecar, rename back (needs the
`fs_rename` from §3).

**Effort.** S–M.

## 25. Cross-cutting: shell settings persistence

Not a user feature but a prerequisite for half the list: a single
`U:/settings.ini` read at `session_begin` and written on change —
theme, layout, pins, icon positions, saver timeout, autologin. Tiny
kv-parser (~120 LOC), one writer `settings_set(key,val)` that marks dirty
and lets the existing debounced `fs_sync` (`hypefs.h:68-73`) flush.
**Hook:** new `kernel/session/settings.cc`; consumers everywhere above.

---

## Summary table

| # | Feature | Reference | Data needed | Hook file(s) | Effort |
|---|---------|-----------|-------------|--------------|--------|
| 1 | Clipboard + ring | Win32 clipboard, GTK GdkClipboard, ICCCM selections | mime+blob slots, 8-entry ring | `kernel/win/xau.cc` (+ new `clipman.cc`), `winman.cc:1572` | M |
| 2 | Drag & drop | OLE DnD, XDND, wl_data_offer | drag session, icon `{col,row}`, pin list | `winman.cc` drag engine (:84-88), `deskicons.cc`, `deskbar.cc:850` | L |
| 3 | File manager | Explorer, Nautilus, GNOME HIG | path, selection, fs ops, `fs_rename` new | `kernel/hxrt/apps/hxfiles.cc` (new), `kernel/vfs/hypefs.cc`, `fdtable.cc` | L |
| 4 | Terminal (hxshell) | Win Terminal, GNOME Terminal | line buf, history, cwd, dispatch | `kernel/hxrt/apps/hxshell.cc` (new), `xau.h` key msgs, `hypefs.h` | M |
| 5 | Text editor | Notepad, GNOME Text Editor | text buffer, caret, dirty, fd | `kernel/hxrt/apps/hxedit.cc` (new), `fdtable.h:33-55`, new `textarea` widget | M |
| 6 | Settings depth | Win Settings, GNOME Settings | `U:/settings.ini`, theme/locale/kbd/audio state | `kernel/hxrt/apps/hxsettings.cc` (59 LOC stub), `theme.cc`, `rtc.cc`, `pullmix.h` | M |
| 7 | Notification center | Win Action Center, GNOME notif list | 16-entry stamped history ring | `kernel/ui/deskbar.cc` (:916,953), `overlays/toast.cc` | S–M |
| 8 | Start search | Win Start type-search, GNOME Activities | query, filtered order, file hits | `deskbar.cc` (:268 build_menu_order), `hypefs.cc fs_list` | M |
| 9 | Pins + jump lists | Win taskbar pins/jump lists, GNOME dash | pin list, per-app recents, settings | `deskbar.cc`, `wm_ctx_open` (`winman.h:80`), `deskman.cc:100` | M |
| 10 | Virtual desktops | Win Task View, GNOME workspaces | `Win::desktop`, cur index | `winman.h:28` struct Win, `winman.cc:689`, `deskbar.cc:820` | M |
| 11 | Task thumbnails | Win7+ taskbar previews | per-win thumb from back buffer | `deskbar.cc` hover, `deskman.cc:39` back_fb / `xdp.h` surface | M |
| 12 | Accessibility | Win Ease of Access, WCAG, AT-SPI | HC theme, sticky latch, zoom, announce log | `theme.cc`, `scancode.cc`, `deskman.cc`, new OSK widget | M |
| 13 | i18n / layouts | XKB, Win+Space, UTF-8 | layout tables, locale fmt, font coverage | `scancode.cc`, `deskbar.cc` tray, `hypefont/hypetext` (Montserrat Cyrillic?) | M |
| 14 | Media keys | HID consumer page, VK_MEDIA_* | usage→action map | `xhci.cc:543-635`, `scancode.cc` E0 codes, `pullmix.h` | S–M |
| 15 | Multi-monitor | Win extend/duplicate, EDID | output list, virtual desktop | `xdp.cc` (:144-153), `backend_virtgpu.cc`, `ptrmv.h:9` | L (S dup) |
| 16 | User accounts | Win Accounts/shadow, GNOME Users | roster, per-user home, autologin, guest | `session.cc`, `userservice.cc`, `hellostore.cc`, `hypefs.cc` | M |
| 17 | Screensaver/idle→lock | Win saver+lock, GNOME blank+lock | idle timer, saver kind, timeout | `main.cc:1049` loop, `session.cc:309`, `lockscr.cc` | S |
| 18 | Help app | Win F1 Help, GNOME Yelp/Mallard | md-subset renderer, `U:/docs` | `kernel/hxrt/apps/hxhelp.cc` (new), `docs/wiki/*.md` | M |
| 19 | Calendar flyout | Win/GNOME clock flyout | RTC month grid | `deskbar.cc` clock cell (:924), `rtc.h` | S |
| 20 | Window animations | Win/mutter transitions | anim recs, frame morph | `winman.cc` paint loop, `ui/anim.h` ticker | M |
| 21 | File associations | assoc hive, mimeapps.list | ext→app table, open-with ctx | `deskicons.cc`, new `kernel/win/assoc.cc` | S–M |
| 22 | Installer UI | MSIX, GNOME Software, AppStream | install dialog, apps list | `hypefile.cc:98`, `widgets/dialog.cc`, `hxsettings.cc`, `deskicons.h:19` | S–M |
| 23 | Screenshot | Win PrtScr/Snipping, GNOME shot UI | BMP encoder, shot counter | `deskman.cc` back buffer, `fs_write`, E037 chord | S |
| 24 | Recycle bin | $Recycle.Bin, freedesktop Trash spec | `.trash/` + `.info` sidecars | new `kernel/vfs/hypetrash.cc`, `deskicons.cc` ctx | S–M |
| 25 | Settings persistence | (infra) | kv file, dirty flag | new `kernel/session/settings.cc`, `fs_sync` (`hypefs.h:68`) | S |

---

## Recommended top-10 (ranked by user-visible value ÷ effort)

1. **Notification center (§7)** — the data path already exists (toast text
   ring, bell badge); a flyout modeled on the QS panel turns transient
   toasts into a real shell feature. S–M.
2. **Terminal hxshell (§4)** — transforms the OS from demo to tool; every
   subsystem (fs, net, svc, apps) becomes inspectable. Composes from
   existing widgets. M.
3. **Screenshot tool (§23)** — PrtScr → BMP via the existing compositor
   back buffer is ~150 LOC total and instantly useful for docs/bugs. S.
4. **Clipboard (§1)** — Ctrl+C/X/V across XAU windows + TextInput; the
   Win+V ring makes it feel like Win10+. M.
5. **Settings persistence + HxSettings depth (§25 + §6)** — theme,
   layout, pins all need the kv store; HxSettings is a 59-line stub
   waiting for the `Tabs`/`Switch` widgets that already exist. S+M.
6. **File manager (§3)** — the biggest item, but `fs_list/fs_mkdir/
   fs_delete` + `ListBox`/`Table` make it mostly UI work; pairs with
   §21 associations and §24 trash. L.
7. **Start-menu search (§8)** — type-to-filter over `hxrt_list` + `fs_list`
   is a small diff on `build_menu_order` with outsized UX payoff. M.
8. **Calendar flyout (§19)** — clock cell is currently a dead click; a
   month grid is pure math over `rtc_now`. S.
9. **Virtual desktops (§10)** — the spec (`desktop-shell.md:201-209`)
   already prescribes `Win::desktop` + filtering; mostly bookkeeping. M.
10. **Screensaver/idle→lock (§17)** — idle tick counter + existing lock
    veil gives auto-lock for near-zero code; demos well on hardware. S.

**Honorable mentions:** taskbar thumbnails (§11) and window animations
(§20) are the two biggest "wow" items once the back-buffer capture path
is factored out — do §23 first since it builds that path. Recycle bin
(§24) should land with the file manager so Delete never ships as
permanent-only.

---

## Watch items / risks found during the audit

- **Montserrat glyph coverage** — `kernel/font/montserrat_ttf_data.cc`
  may lack Cyrillic; verify `ttf.cc glyph_index` for U+0410..U+044F
  before promising RU layout rendering (§13).
- **`wm_ctx_max_items = 8`** (`winman.h:49`) caps context menus — jump
  lists and "Open with" submenus may need a paged/scrollable variant.
- **TextInput is single-line** — §4/§5/§18 all need a multi-line text
  surface; build `kernel/ui/widgets/textarea.cc` once, reuse thrice.
- **Brightness slider is a stub** — `qs_bright` (`deskbar.cc:78`) writes
  only a serial marker; §6 should either wire a real backlight path
  (ACPI/`monitor-control-research.md` exists) or drop the row.
- **Lock/Power buttons in QS are markers only** — `deskbar.cc:886-895`
  prints `power action=` to serial instead of calling
  `session_lock()`/`session_power_dialog()` which already exist in
  `session.h:27,32`. Trivial fix, fold into §6/§17.
- **Fixed caps to keep in mind** — `wm_max_windows=16`,
  `fs_max_entries=192`, `xau_post_capacity=64`,
  `AnimTicker::max_widgets=16`, `fs_name_size=32`: several features
  (recycle bin sidecars, thumbnail cache, jump-list recents) push these
  limits; audit before each feature lands.
