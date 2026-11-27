# Shell & Runtime — design brief (P9 research)

Scope: user-program layer for hypeaios — `.hxer` executables, `RevServices`,
session/login flow, `HYPEOS://` paths, `winman`, app registry. Freestanding
x86-64 kernel, no libc, no dynamic linking, no disk (QEMU, VFS = memfs).
Aligns with roadmap P9 (`docs/roadmap.md`, S1–S9). Existing building blocks:
`hype::ui::Canvas` immediate-paint API (`kernel/ui/canvas.h`), retained
`Widget` tree (`kernel/ui/widget.h`), `FocusManager` (`kernel/ui/focus.h`),
`hello` auth with password/PIN/ECDSA-ish methods + `fail_count` lockout
(`kernel/hello/hello.h`), codec probe registry
(`kernel/codecapps/registry.h`), `lmh::ExecuteRequest` process/task stubs
(`kernel/lmh_executeService.h`).

---

## 1. Executable / app formats in hobby OSes

**What exists**

- SerenityOS ships ELF binaries in `/bin`; a separate INI-style Application
  File (`*.af`, no magic) in `/res/apps` provides `{Name, Executable,
  Category, IconPath, RunInTerminal, FileTypes, Protocols}` for the system
  menu and LaunchServer. Manifest is *divorced* from the binary.
  <https://man.serenityos.org/man5/af.html>
- Haiku/BeOS appends a `resources` blob to the end of the ELF (not a proper
  ELF section — `strip` removes it) carrying `app_signature`
  (`application/x-vnd.*`), `app_flags` (B_SINGLE_LAUNCH), `app_version`,
  vector icon. Packaging is layered `.hpkg` (TOC + heap + package attributes,
  big-endian/LEB128) mounted by packagefs.
  <https://www.haiku-os.org/docs/develop/packages/FileFormat.html>
  <https://medium.com/@probonopd/my-sixth-day-with-haiku-under-the-hood-of-resources-icons-and-packages-abec8d0e4ec6>
- Redox uses `pkgar`: 136-byte header {NaCl sig, pubkey, BLAKE3 hash of
  entry table, entry count} — verify-then-extract, relocatable packages.
  <https://doc.redox-os.org/book/package-management.html>
- Kernels without dynamic linking skip file formats entirely and use
  *linker-section registries*: Linux `__initcall` puts fn pointers in
  ordered `.initcallN` sections kept by `KEEP()` in the linker script;
  Zephyr `SYS_INIT` does the same with levels + priorities.
  <https://github.com/torvalds/linux/blob/master/include/linux/init.h>
  <https://docs.zephyrproject.org/apidoc/latest/group__sys__init.html>

**Minimal viable design for hypeaios**

Two-layer model, exactly what codec registry already does:

1. **Built-in table** — a `static const HxerDescriptor g_hxer_apps[]` array
   (or `.hxer` linker section like initcalls if ordering matters). This is
   v1; all apps are statically linked kernel objects.
2. **`.hxer` file format** — future-proof binary header prepended to a blob
   stored in memfs at `HYPEOS://S/apps/`:

```c
struct HxerHeader {            // 64 bytes, little-endian
    hcpp::u32 magic;           // 'HXER' = 0x52455848
    hcpp::u16 hdr_version;     // 1
    hcpp::u16 flags;           // HXER_F_SYSTEM (.hxersys), HXER_F_AUTORUN,
                               // HXER_F_SINGLE_LAUNCH, HXER_F_NEEDS_WINDOW
    char     name[24];         // display name, UTF-8
    hcpp::u16 ver_major, ver_minor, ver_patch, ver_build;
    hcpp::u32 entry_offset;    // offset to code/entry blob (file mode)
    hcpp::u32 entry_symbol;    // registry id / fn ptr (builtin mode)
    hcpp::u32 icon_offset;     // optional embedded icon blob
    hcpp::u32 crc32;           // header+payload checksum
};
using HxerEntry = hcpp::u32 (*)(hype::ui::Canvas& canvas,
                                hcpp::StringView args) noexcept;
```

Entry-point ABI: `entry(Canvas&, args)` returning an exit code — same shape
as `lmh::ExecuteRequest{process_id, entry, argument, capabilities}`
(`kernel/lmh_execute.h:7-12`). `.hxersys` = `HXER_F_SYSTEM` + delete guard.

**Pitfalls**: descriptor-without-magic (SerenityOS `.af`) forces extension
sniffing — keep a real magic. Split manifest/binary doubles failure modes;
embed header in the `.hxer` file. Linker GC will drop table entries — mark
`KEEP()` or reference them from a report function like
`codec_register_builtins()`. Version the header from day 1.

---

## 2. Service managers

**What exists**

- systemd `.service`: `[Unit] Description/After`, `[Service]
  ExecStart/Type/Restart={no,always,on-failure}/RestartSec`, `[Install]
  WantedBy`. Journald gives per-service logs.
  <https://freedesktop.org/software/systemd/man/latest/systemd.service.html>
- Android init `.rc`: `service name /path args` + options (`class`,
  `disabled`, `oneshot`, `onrestart`, `socket`) + `on trigger` →
  `start/class_start/restart` commands.
  <https://android.googlesource.com/platform/system/core/+/main/init/README.md>
- Windows SCM: programmatic — `OpenSCManager/OpenService/StartService/
  ControlService(STOP|PAUSE|CONTINUE|INTERROGATE)/QueryServiceStatusEx`;
  services report states via `SERVICE_STATUS` (START_PENDING, RUNNING,
  STOPPED...) through `SetServiceStatus`.
  <https://learn.microsoft.com/en-us/windows/win32/services/service-control-manager>
  <https://learn.microsoft.com/en-us/windows/win32/services/service-control-requests>

**Minimal `RevServices` design**

```c
enum class RevSvcState : u8 { Stopped, Starting, Running, Stopping, Failed };
enum class RevSvcRestart : u8 { Never, OnFailure, Always };
struct RevService {
    char name[24];
    RevSvcState state;
    bool autostart;                    // started during boot, behind greeter
    bool system;                       // cannot be stopped/deleted
    RevSvcRestart restart;
    u8   restart_max;                  // cap restarts, then Failed
    RevSvcEntry entry;                 // bool (*)(void* ctx) — run/tick fn
    void* ctx;
    u8   log_ring[LOG_RING_BYTES];     // circular char buffer per service
    u32  started_ticks, fail_count;
};
// ops: revsvc_register, revsvc_start/stop/restart(name),
//      revsvc_state(name), revsvc_list(out), revsvc_log_read(name, buf)
```

Boot: after `core_services` init, iterate registry → start all
`autostart` entries on the scheduler *before/until* the greeter renders
(services background, greeter foreground — the GDM pattern, §3).

**Pitfalls**: a "service" here is a kernel task, not a process — no fork()
semantics; keep `entry` cooperative (tick-based) or give it its own
`lmh::task`. Log ring must be per-service fixed size (Android/systemd both
keep logs external; we have no FS persistence). SCM's serial-notification
blocking (30 s timeout) shows why control ops must not run inline in UI
thread — dispatch via scheduler.

---

## 3. Login / session flows

**What exists**

- Windows: WinLogon spawns LogonUI on a secure desktop after SAS;
  credential *providers* enumerate tiles (password/PIN/Hello bio), providers
  only gather+serialize creds — LSA enforces. Fast-user-switching keeps N
  concurrent sessions; Win10 merged `CPUS_LOGON`/`CPUS_UNLOCK_WORKSTATION`
  so unlock==logon.
  <https://learn.microsoft.com/en-us/windows/win32/secauthn/credential-providers-in-windows>
  <https://learn.microsoft.com/en-us/windows/win32/shell/fast-user-switching>
  <https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/ne-credentialprovider-credential_provider_usage_scenario>
- PAM transaction: `pam_start → pam_authenticate → pam_acct_mgmt →
  pam_setcred → pam_open_session … pam_close_session → pam_end`.
  <https://docs.freebsd.org/en/articles/pam/>
- GDM: privileged daemon per seat; starts an *unprivileged greeter session*
  (gnome-shell instance) which talks to a `gdm-session-worker` that runs
  the PAM stack; on success worker opens the user session. Services/agents
  may run inside the greeter session. Logind integration = "watch seats,
  spawn greeter, run PAM stack".
  <https://wiki.gnome.org/Projects(2f)GDM(2f)NewDesign.html>
  <https://github.com/GNOME/gdm/blob/main/daemon/gdm-session-worker.c>

**Minimal hypeaios session flow**

```
boot → RevServices autostart (background tasks: audio, net?, wallpaper prep)
     → winman creates greeter window (topmost, fullscreen)
     → greeter = existing flowlogin UI → hello_auth(uid, method, secret)
         ├── NotEnrolled → registration flow (hello_register_user +
         │                 hello_enroll(password|pin)) — first-boot path
         ├── LockedOut   → show lockout, no retry until timer
         └── Ok          → open Session {uid, method, start_ticks}
                           → spawn DXBackgroundDesktop + SpeedStart,
                             close greeter window
lock: session stays, greeter window re-shown (unlock == logon, Win10 model)
switch user: later — keep single active session v1, API shape allows N
```

`hello` already provides the "LSA" (enforcement) and `fail_count` lockout;
greeter/flowlogin is the "credential provider" (gather only). Keep that
split: UI must never see the verifier, only `HelloResult`.

**Pitfalls**: don't run service entries synchronously during greeter paint —
one stuck service freezes login (systemd parallelizes for this reason).
Registration must force enrolling ≥1 method before session opens (Windows'
"always keep a system credential provider" rule). Lock ≠ logout: preserve
window state, only raise greeter above all z.

---

## 4. URI path schemes

**What exists**

- `file:///path` — RFC 8089, scheme + optional empty authority + abs path.
  <https://www.rfc-editor.org/info/rfc8089.html>
- `content://authority/path/id` — authority routes to a registered
  ContentProvider; provider interprets rest of URI however it wants.
  <https://developer.android.com/reference/android/content/ContentUris>
  <https://developer.android.com/guide/topics/providers/content-provider-creating>
- DOS drive letters `C:\path` — per-volume roots, ambient current-drive.
- Plan 9: `#<char>` device roots bound into per-process namespaces
  (`bind("#c","/dev")`); a single char identifies the *driver*, path after
  it is the device's own tree — the direct ancestor of `HYPEOS://<char>/`.
  <https://plan9.io/sys/doc/names.html>
  <http://9p.io/magic/man2html/3/0intro>

**Recommended `hypepath` resolver**

```
HYPEOS://<DiskChar>/<path>        case-insensitive scheme, '/'-separated
  S = system volume  (read-only-ish; .hxersys live here, non-deletable)
  U = user volume    (per-user files; root = HYPEOS://U/<uid>/ later)
  T = temp volume    (wiped at boot; scratch, logs)
  R = resources?     (reserved; read-only embedded blobs — deferred)
```

Resolver splits `HYPEOS://S/apps/SpeedStart.hxersys` → `{vol:'S',
path:"apps/SpeedStart.hxersys"}` → vfile on that volume's memfs root.
Content-URI lesson worth stealing: the char selects the *provider* (like
authority), so `HYPEOS://D/...` could later map to a real AHCI disk without
API change.

**Pitfalls**: RFC 8089 authority confusion — `HYPEOS://S` is authority-`S`,
not scheme-relative; document that `HYPEOS:S/path` is invalid. Reserve
chars now (A–Z single char only). Temp volume must exist even before disks
or every temp writer needs a fallback.

---

## 5. Window manager essentials

**What exists**

- OSDev GUI pattern: back buffer → paint bottom-up: desktop bg, then each
  window in z-order, then cursor; each window paints chrome + content, no
  per-window buffers needed for a clipping-based WM.
  <https://wiki.osdev.org/GUI>
- Compositing alternative (per-window surfaces) costs memory + a blitter;
  classic clip-rect WM repaints exposed regions on demand — fine at
  framebuffer resolutions.
  <https://wiki.osdev.org/Compositing>
- OSDev forum consensus on bookkeeping: window {id, owner, rect, z-list
  ptr, tree ptrs}; z-order as linked list, top-to-hit-test from front,
  paint from back; per-window visible-rect list for clipping.
  <https://forum.osdev.org/viewtopic.php?t=30428>
  <https://f.osdev.org/viewtopic.php?t=10399>

**Minimal `winman` design**

```c
struct WmWindow {          // capacity ~16, static array
    u8  id; u32 owner;     // owner = hxer app id or session
    Rect frame;            // outer; content = frame inset by chrome
    WmFlags flags;         // HAS_CLOSE | HAS_MENU | NO_FOCUS | TOPMOST(greeter)
    Widget* content_root;  // retained widget tree OR
    HxerEntry paint_fn;    // immediate painter for .hxer apps
    bool dirty;
};
// chrome: title bar (name + close ▢ + menu ☰), 24px
// ops: wm_open(desc) → slot, wm_close(id), wm_focus(id),
//      wm_raise/wm_lower, wm_paint_all(canvas), wm_hit_test(x,y)
// focus: single FocusManager instance already exists (kernel/ui/focus.h)
//        — wm_focus() hands key routing to window's widget tree
```

Paint loop fits Canvas: `paint_desktop → for each window back→front:
clip to visible region, draw chrome, content.paint_tree(canvas, clipped)`.
Decorations are *non-client* (drawn by winman, not the app) — same split as
Windows DWM vs app content.

**Pitfalls**: immediate paint means repainting an under-window when the top
window moves/closes — keep it simple: full repaint on any z/move event at
v1, dirty-rect later. Hit-test chrome before content (close button must
win). Modals (toast/dialog overlays already exist in `kernel/ui/overlays`)
should be a separate always-above-windows layer. No pointer input yet —
design focus/close ops around keys + future mouse.

---

## 6. App store / registry

**What exists**

- macOS `.app` bundle: `Contents/Info.plist` {CFBundleExecutable,
  CFBundleIdentifier} + `MacOS/` binary + `Resources/`; bundle id is the
  stable identity used by LaunchServices, prefs, signatures.
  <https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFBundles/BundleTypes/BundleTypes.html>
- freedesktop `.desktop`: `[Desktop Entry] Type/Name/Exec/Icon/MimeType`;
  reverse-DNS file names; located in `applications/` dirs.
  <https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html>
- Android marks apps in `/system` partition non-deletable (ro mount +
  system flag on the PackageManager record); Windows inbox apps similarly.

**Minimal design**

Single `hxrt` app registry = table of `HxerDescriptor` populated from (a)
built-in table, (b) memfs scan of `HYPEOS://S/apps/*.hxer{,sys}` and
`HYPEOS://U/apps/*.hxer`. Fields: {header, origin VolChar, state
Installed/Running}. Non-deletable = `flags & HXER_F_SYSTEM` **or** located
on `S:` — belt & suspenders; `hypefile` delete op refuses both.
`CFBundleIdentifier`-style unique `app_id` string in header (e.g.
`os.hypeai.SpeedStart`) so references survive renames.

**Pitfalls**: identity by filename breaks on rename — keep app_id in the
header. Scanning at every launch is fine at ≤16 apps but put results in
registry once; file-watch later.

---

## 7. QEMU-testable bits

- **virtfs/9p on QEMU for Windows hosts: effectively absent.** `-virtfs`
  (virtio-9p, `local` backend) requires POSIX dirfd APIs; upstream Windows
  builds don't ship it ("virtio-9p (virtfs) requires Linux or macOS").
  An unmerged 2022 patchset exists; forks (`arixmkii/qcw`) got RO-only.
  <https://github.com/lima-vm/lima/issues/909>
  <https://github.com/virtio-win/kvm-guest-drivers-windows/issues/126>
  <https://lore.kernel.org/qemu-devel/20220425142705.2099270-1-bmeng.cn@gmail.com/>
  virtio-*guest* drivers for Windows are also nonexistent for 9p, but
  irrelevant — hypeaios is its own guest and would need an in-kernel 9p
  client anyway. **Verdict: do not plan on 9p.**
- Working alternatives on Windows QEMU today:
  - embed blobs into the kernel image at build time (linker `.rodata`
    section or `tools/` packer emitting a C array) — how `demo_image_blob.h`
    and fonts already work; **recommended for v1 memfs seeding**.
  - `-initrd` multiboot module → kernel reads it as a flat archive.
  - `-fw_cfg name=...,file=...` → tiny MMIO/IO read interface, good for
    config blobs, no driver work.
  - serial file transfer over `-serial` (we already have serial logging);
    or FAT disk image built by a `tools/` script once AHCI/ATA lands.
- All of §1–6 is testable headless: paint to framebuffer, drive keys over
  serial, assert via serial log (`Usersalinoqemu-serial.log` pattern).

---

## 8. Recommended ticket breakdown — "Shell & Runtime" program

| #  | Ticket | Depends |
|----|--------|---------|
| S1 | `hxrt` runtime — `HxerHeader` {magic,name,ver,entry,flags SYS/USER}, app registry, exec into window slot | — |
| S2 | `winman` — window chrome (title/close/menu), focus, z-order, hit-test, app window slots | S1 |
| S3 | `hypepath` — `HYPEOS://<S\|U\|T>/` resolver + memfs volumes in vfile | — |
| S4 | `RevServices` — registry {name,state,entry,log_ring,autostart,restart}, start/stop/restart/query ops, boot autostart sweep | — |
| S5 | `RevDBGService.hxersys` — service list UI: restart/stop/log-ring viewer | S2,S4 |
| S6 | `DXBackgroundDesktop.hxer` — wallpaper: codec probe/decode → desktop bg layer under z-order | S2 |
| S7 | `SpeedStart.hxersys` — launcher grid over app registry, launches `.hxer` via hxrt | S2,S3 |
| S8 | Session — registration + login gate via `hello_auth`; autostart services run behind greeter; lock==logon; demo.cc removed | S2,S4 |
| S9 | `hypefile` — `.hxer`/`.hxersys` entries at `HYPEOS://S/apps/`, delete guard on system files | S3,S1 |
| S10 | `hxpack` tool — build-time `.hxer` packer + memfs seed blob generator (replaces 9p path) | S1,S3 |

Order of attack: S3+S4 independent (parallel with S1), S2 unlocks all UI
apps, S8 last-ish so demo removal lands on a working shell.
