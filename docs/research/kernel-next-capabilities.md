# HypeAI OS — Next Kernel Capabilities

Date: 2025-11-28 · Branch: `devin/p22-res-kern`

Scope: what major capabilities to build **next**, scanned against what peer
hobby OSes (SerenityOS, Managarm, ToaruOS/Misaka, Redox, skiftOS/Hjert) and
small production OSes (Linux/FreeBSD/OpenBSD subsets) actually ship. Every
row names the peer feature it mirrors and the exact hook point in *this*
tree.

Brother docs (read first): `docs/research/gaps-kernel.md`,
`docs/research/gaps-hardware.md`, `docs/research/gaps-desktop.md`,
`docs/research/core-os-improve.md`, `docs/research/os-core-roadmap.md`.

Effort grades: **S** < 1 day / < ~300 LoC — **M** 1–3 days / ~300–800 LoC —
**L** ~1–2 weeks / ~800–2500 LoC — **XL** multi-week / 2500+ LoC.

---

## 0. Ground truth — what changed since `gaps-kernel.md`

That audit (pre-#137 era) proposed a scheduler, kernel threads, TCP, DNS,
NTP, crash dumps, watchdog, fbcon, a bytecode VM and a registry. All of
those **now exist**, so this doc does not re-propose them:

- Preemptive SMP scheduler + kernel threads — `kernel/sched/thread.cc`
  (32-slot table, per-thread 16 KiB stacks, affinity, `sleep_ms`/`yield`/
  `join`; LAPIC tick dispatch). Watchdog deadline check runs in the timer
  path (`kernel/sched/watchdog.cc`, `idt.cc:46`).
- TCP — `kernel/net/tcp.{h,cc}` (`tcp_connect`/`tcp_listen`/`tcp_send`/
  `tcp_recv`/`tcp_close`, 8 KiB rx window). DNS — `kernel/net/dns.cc`.
  NTP — `kernel/net/ntp.cc` (`ntp_sync`, `net_time_unix`; RTC kept
  read-only). ping — `net_icmp_echo_send` in `kernel/net/net.cc`.
- Crash dumps — `kernel/log/crashdump.cc` + `kring.cc` ring sink;
  `panic.cc` dumps register fields (`write_field("rbp", ...)` at
  panic.cc:122).
- fbcon — `kernel/console/fbcon.cc`. Settings persistence —
  `kernel/cfg/settings.cc` (hypeblob-backed). Bytecode VM —
  `kernel/hxbvm/` (`.hxbc` apps registered as `.hxer`; `hxbvm_boot()` at
  main.cc:785). hxbvm's existence also makes it the natural substrate for
  in-kernel sandboxed programs ("kernlets", §8.4).
- e1000 has a real IRQ handler (`kernel/net/e1000.cc:149`); **virtio-net
  RX is still poll-driven** (ISR ack read at virtio_net.cc:298, delivery
  via `net_poll()`).

Still open from the old audit (carried forward here): userspace/ring-3/
ELF, demand paging/swap, TLS, IPC pipes+shm, event model, kernel modules,
DTrace probes, S3, and the SMP locking audit — `sched/thread.h` still
warns "IRQ paths and legacy global state are NOT thread-safe" and the
boot thread stays BSP-pinned.

What peer OSes ship that we still lack (surveyed 2025-11-28):

- **SerenityOS** — ring-3 userspace, ~150 syscalls, `pledge()`/`unveil()`
  (promise-mask syscall filtering + path hiding), `sendfd`/`recvfd`
  (fd passing over sockets), `futex()`, `dump_backtrace()`, anon shared
  buffers, `mount`/`bindmount`, procfs.
- **Managarm** — asynchronous syscalls with a lock-free completion ring
  (io_uring shape), `hel` IPC streams with capability transfer, `mbus`
  service-discovery bus, **kernlets** (small safe programs compiled into
  kernel space, eBPF-style), clocktracker service, posix subsystem with
  epollfd/signalfd/timerfd/inotify, userspace ext2 via libblockfs.
- **ToaruOS/Misaka** — userspace compositor (Yutani) whose windows are
  **shared-memory buffers** owned by clients + `pex` packet-exchange
  pipes for commands, kernel modules, dynamic linker, own libc, SMP.
- **Redox** — *schemes*: URI-namespace IPC where `open("scheme:/path")`
  routes file ops to a userspace daemon via SQE/CQE message packets;
  `event` scheme (fd-readiness epoll); `ipcd` shm/chan channels;
  drivers as userspace daemons.
- **skiftOS/Hjert** — capability-based object model, RPC-style IPC ports,
  jobs, and an **mmap-able clock page** (vDSO-style kernel→user
  read-only data page).

---

## 1. Process / userspace — the flagship gap

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 1.1 Ring-3 + GDT/TSS | Every peer OS runs apps in ring 3. SerenityOS/ToaruOS/Misaka all: user GDT segments + per-CPU TSS (rsp0 stack switch, IST for #DF/NMI). Intel SDM Vol 3A §6–7. | New `kernel/arch/x86/gdt.{cc,asm}` (kernel owns its GDT — today we inherit stage2's, see `ap_tramp.asm` notes) + `tss.{h,cc}` per-CPU; IST stacks also fix double-fault robustness regardless of userspace | M | Gate to everything below |
| 1.2 Per-process address spaces | Peer kernels give each process its own PML4 with the kernel half shared; CR3 swapped on context switch; TLB shootdown IPI. Managarm/Redox push further (per-space mmo objects) | `kernel/memory/vmm.cc`: `vmm_create_space()`/`vmm_clone_kernel_half()`; `sched::Thread` gains `cr3` field switched in `context.cc`/`context.asm`; resched IPI doubles as shootdown vector | L | Memory protection; enables 1.3/2.x |
| 1.3 syscall/sysret ABI | All peers: `syscall` insn via STAR/LSTAR/SFMASK MSRs, SysV arg regs. SerenityOS: `Kernel/API/Syscall.h` table | New `kernel/arch/x86/syscall_entry.asm` → dispatch onto existing `kernel/syscall.cc` (grow past 10 slots); `int 0x80` fallback gate; HypeGuard `guard_check` re-anchors subject=current thread | M | Real app/kernel boundary |
| 1.4 ELF64 loader | Every peer loads ELF64 PT_LOAD segments (SerenityOS/ToaruOS/Redox all ELF; Managarm loads thor+servers) — osdev "ELF", SysV gABI | New `kernel/hxrt/elf.cc`: PT_LOAD→`vmm_map_page(user)`+PMM frames, entry→user iret frame. `.hxer` becomes a packaging wrapper around the ELF blob (`hxpack.cc` header already has the shape); apps loaded from `U:/apps/` via `fs_open`/`fs_fread` | M | Loadable native apps |
| 1.5 Per-task exception recovery | Linux `do_page_fault`/`die()` split: #PF/#GP in a task kills the task, not the box. SerenityOS kills the process and logs | `idt.cc:101` exception branch: consult `sched::current()` first; if a thread context, mark Dead + record rip/cr2 + reschedule; only idle/ISR faults stay `panic_exception`. Needs TSS IST (1.1) for #DF | M | Reliability multiplier; pairs with crashdump |

## 2. Memory

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 2.1 Demand paging (#PF fault path) | Linux `vm_fault`, FreeBSD `vm_fault`: not-present faults allocate lazy zero pages / grow stacks instead of panicking. Managarm even lets userspace resolve faults | `kernel/memory/vmm.cc`: `vmm_page_fault(cr2, err)` consulted *before* `panic_exception` in `idt.cc`; PTE-preserved bit for demand-zero regions. Today #PF always panics (idt.cc:90–101) | M (fault path) | Lazy alloc for every later subsystem |
| 2.2 Swap + OOM killer | Linux `mm/oom_kill.c` + swap slots encoded in not-present PTEs | New `kernel/memory/swap.{h,cc}` over a fixed blockdev LBA range (`block/blockdev.h`, same trick as hypeblob), `memory/oom.cc` picks fattest thread/app. PMM is small — an OOM policy matters | L | Soft memory ceiling |
| 2.3 mmap-ish regions (VMA list) | All peers track per-space VMAs (Linux `mm_struct`->maple tree; skift mmo objects); file-backed views need 2.1 | `vmm.cc` gains a region list per address space; `SyscallNumber::Map` (syscall.h already reserves nr 5) becomes the entry point | L | Shared mem (§5), file views, libc malloc |
| 2.4 Huge pages (2 MiB/1 GiB) | Linux THP/`MAP_HUGETLB`; trivial on x86 (PDE/PDPTE PS bit) | `vmm_huge_page` flag + translation already exist (vmm.cc:68–72); missing: `vmm_map_huge()` + PMM alignment-aware allocation in `pmm_allocate_pages` | S | TLB relief for framebuffer/heap; cheap |
| 2.5 Shared kernel→app data page (vDSO-lite) | Linux vDSO clock; skift literally made its clock mmap-able (commit 7c34f0a) | Map one RO page into every address space carrying `uptime_ns`, tick rate, kernel version; `SyscallNumber::Query` becomes a page read | S | Free clock/version reads post-userspace |

## 3. Networking

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 3.1 Unified socket API on the fd table | POSIX `socket()`/`connect()`/`accept()` as fds; Redox maps sockets to `tcp:` scheme files; Managarm posix-subsystem sockets | `kernel/vfs/fdtable.cc` gains `fd_kind::Socket` wrapping `TcpConn*`/`net_udp_*` so `fs_fread/fs_fwrite` work on sockets; syscall layer once 1.3 lands | M | Everything net-facing becomes a file |
| 3.2 HTTP/1.1 client lib | SerenityOS LibHTTP+LibTLS power its browser/update pull; ToaruOS ports curl | New `kernel/net/http.{h,cc}` over `tcp_*` + `dns_resolve` (both exist); chunked+content-length responses | M | Fetch/update channel; first "real" net app |
| 3.3 TLS 1.3 client + X.509 | SerenityOS LibTLS/LibCrypto; BearSSL as minimal reference. We already have `hcpp/sha256` (HKDF core), `hcpp/ecdsa` (cert verify), RDRAND `hcpp/rng` | `hcpp/chacha20|poly1305|x25519` (~600 LoC RFC 8439/7748) + `kernel/net/tls.cc` record/handshake + `net/x509.cc` DER parse + root bundle on `S:/data/` via hypeblob | L | HTTPS → update channel, package pulls |
| 3.4 IRQ-driven virtio-net RX | Legacy virtio ISR → used-ring drain; e1000.cc already does this correctly (`irq_handler`, line 149) | `kernel/net/virtio_net.cc`: register handler via `idt_vector_register` (INTx or MSI per `pci_irq.h`), ack ISR (line 298 already reads it), wake netd instead of relying on `net_poll()` | S/M | Latency; IRQ→thread template for AHCI/xHCI |
| 3.5 IPv6 | Linux dual stack; NDP (RFC 4861) replaces ARP, ICMPv6, SLAAC autoconf | `kernel/net/`: new `ip6.{h,cc}` parallel ethertype path + `ndp.cc`; udp/tcp get an address-family flag; e1000/virtio unchanged | L | Real-network reachability; low urgency under QEMU slirp |

## 4. Storage / filesystems

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 4.1 Journaling/write-ahead for persistence | ext3/NTFS journal; even a bounded redo log survives power loss mid-sync | `kernel/vfs/hypeblob.cc`: seq-numbered redo records at a fixed LBA range replayed at mount before commit; today a torn HYPEDATA write corrupts the whole blob | M | Persistence you can trust on real hw |
| 4.2 ext2 read | Managarm runs ext2 via libblockfs; every Linux LiveISO mounts ext2; great "foreign fs" proof for the VFS layer | New `kernel/vfs/ext2.cc` read-only: superblock→group descriptors→inode→direct block map; mounts via the same `hypefile`/`vfile` layer | M | Interop (Linux dual-boot data disks) |
| 4.3 tmpfs on demand | Linux tmpfs; our memfs (hypefs) is *effectively* tmpfs but only as fixed S:/U:/T: disks | `kernel/vfs/hypefs.cc` already is the impl — add `fs_mount("T:", new_tmpfs)` runtime mount instead of boot-fixed disks; small | S | Scratch space for apps/tests |
| 4.4 Pseudo-fs for kernel state (procfs/devfs-style) | Linux `/proc`, Managarm kernfs/kerncfg server, SerenityOS procfs; exposes task/mem/net tables as files | New `kernel/vfs/hypeproc.cc` mounted at `P:`: read-only files generated on open from `sched` table, PMM stats, net state, revsvc registry — reuses the fd table read path | S/M | Introspection without new UI |

## 5. IPC between apps (threads now exist — this is unblocked)

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 5.1 Message ports | BeOS ports (closest cultural match — XAU is already BeOS-flavored); skift RPC ports; Managarm hel streams | New `kernel/ipc/port.{h,cc}`: named port registry + bounded queue per port, blocking send/recv on `sched::sleep`/`wake`; XAU's proven 64-entry posted-message FIFO (`kernel/win/xau.h:80,118`) is the in-tree shape to generalize | M | Apps become a system, not islands |
| 5.2 Pipes on the fd table | POSIX `pipe(2)`; ToaruOS `pex` pipes drive its compositor protocol | `kernel/ipc/pipe.cc`: ring buffer + block-on-empty/full; `fdtable` gains `fd_kind::Pipe` so `fs_fread/fs_fwrite` just work; named pipes visible as `U:/pipes/<name>` | M | Byte-stream IPC riding existing fds |
| 5.3 Shared memory objects | SerenityOS `shared_buffer`, ToaruOS window textures in shm, Redox `shm:` scheme | `kernel/ipc/shm.cc`: until 1.2 lands, guard-checked shared heap buffers (`GuardOp::Ipc` policy via `kernel/guard/`); after address spaces: real shared frames via `vmm_map_page` into both CR3s | M | Zero-copy bulk transfer (window buffers!) |
| 5.4 fd/capability passing | SerenityOS `sendfd`/`recvfd`; Managarm transfers capabilities inside hel streams | Port messages carry an optional fd payload; `fdtable` duplicates the entry into the receiver's table | M (after 5.1) | Handing windows/sockets/pipes between apps |
| 5.5 Event queue (epoll/kqueue shape) | Linux `epoll`, FreeBSD `kqueue`, Redox `event` scheme, Managarm epollfd/signalfd/timerfd | New `kernel/event/evq.{h,cc}`: `evq_add(fd|timer|port, mask)` → block until ready; aggregates pipes, sockets, XAU events | M | One non-busy-wait API for everything |
| 5.6 Futex | Linux `futex(2)`, SerenityOS `SC_futex`: sleep on an address's value | `kernel/sched/`: keyed wait-queue map `futex_wait(addr,val)/futex_wake(addr,n)`; mutex.cc already exists but is recursive/kernel-global — futex gives user-bound apps their own | S/M | Cheap sync primitive for the app ABI |

## 6. Scheduler maturity (found this scan)

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 6.1 Event-driven blocking (wait queues wired into sched) | Linux `wait_event`/`wake_up`: threads block on *conditions*, not just time. Ours only has `sleep_ms` (wake_at_ns scanned by the tick — `scheduler.cc:100`) | `kernel/wait_queue.{h,cc}` exists but isn't wired into `sched::sleep`; add `sched::block_on(wq)`/`wake_one/all` and make join/pipes/sockets/ports use it | S/M | Prerequisite for 5.x and event-driven drivers |
| 6.2 SMP locking audit (carry-over) | Per-subsystem lock protocol before threads multiply | `sched/thread.h` comment admits "IRQ paths and legacy global state are NOT thread-safe"; audit pmm/heap/vfs/net/pullmix/winman/revsvc/klog with existing `spinlock/mutex` primitives + add IRQ-save variant | M | Correctness gate; grows more urgent per thread added |

## 7. Input

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 7.1 Touchpad absolute mode + gestures | Linux `psmouse` Synaptics absolute mode + `libinput` gestures (2-finger scroll, tap, edge scroll); USB HID Digitizer page (usage 0x0D) is the USB path | `kernel/input/`: new `touchpad.cc` emitting absolute `HypePtr` packets (extend `hypeptr.h` with abs x/y + contact count); `winman.cc` scroll gesture → wheel events | M | Real-laptop usability |
| 7.2 IME groundwork | Windows IMM-IME / Linux IBus start with: keymap layers, dead keys, compose table, an input-context object per focused window | `kernel/input/keyring.cc`/`scancode.cc`: add keymap table + dead-key state; `kernel/ui/focus.cc` tracks the focused window's input context so an IME can sit between key events and text widgets | M | Non-Latin input later; dead keys cheap now |

## 8. Graphics

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 8.1 virtio-1.0 modern transport | virtio-pci capability-list transport (common/device/notify/ISR cfg BARs) — required by *every* modern-only virtio device; today `backend_virtgpu.cc:395` and virtio-net skip them entirely | `kernel/virtio/vring.cc` + new `virtio/modern.cc`: PCI cap walk (pci_capability.h has IDs), MMIO BAR maps; then gpu/net/rng get a modern path beside the legacy I/O one | M | Unlocks modern-only devices + MSI-X virtqueues |
| 8.2 Runtime modesetting API | Linux KMS `drm_mode_set`; SerenityOS DisplayConnector modeset | `kernel/display/vgadispi.cc` already has `dispi_set_mode` (line 151) for bochs; generalize: `xdp_mode_set(w,h,bpp)` routed per-backend (dispi regs, virtio-gpu `SET_SCANOUT`+EDID pick — `query_edid()` at backend_virtgpu.cc:256 already reads modes), fbcon/XAU re-layout on change | M | Resolution switching at runtime |
| 8.3 GPU compute offload | SerenityOS sketched GL offload; skift plans karm-gpu compute; `XDP_CAP_COMPUTE` flag is already reserved in xdp.h:45 | Software first: `xdp` compute queue executing kernels (blur/scale/YUV conv) on APs via sched threads — SMP already online; real virtio-gpu `VIRTIO_GPU_CMD_*` 3D/blob path is XL and behind virgl (declared out of scope in roadmap §2) | M (sw) | Uses the idle APs; honest first step to GPU accel |
| 8.4 Kernlets — sandboxed in-kernel programs | Managarm `kernletcc` compiles small safe programs ("lewis") that run in kernel space — the eBPF pattern | `kernel/hxbvm/` is already a sandboxed bytecode VM: add a `kernlet` mode where a `.hxbc` blob hooks probe sites (net RX, vfs ops, sched tick) — no new runtime needed | M | Observability + policy without recompiles |

## 9. Developer experience / observability

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 9.1 Serial RX + kernel monitor | Every serious hobby kernel has a debug shell on COM1 (SerenityOS kernel debugger, ToaruOS kernel tasklets, Linux sysrq) | `kernel/serial.{cc,hpp}` is **TX-only today** — add RX ring + poll of LSR; `kernel/console/kmon.cc`: commands (threads, mem, pci, vfs, net stat, kill, crash) dumping the tables we already have | S/M | Debug live state without the GUI |
| 9.2 Symbolized backtraces via kernel.map | SerenityOS `dump_backtrace()` + symbolication; Linux frame-pointer walk + kallsyms | Build already emits `kernel.map` and `tools/mkimage.py:70,125` parses it — embed a compact {addr,name} blob into the image; `panic.cc` walks the rbp chain (rbp is already dumped at panic.cc:122) and prints `func+off` | M | Every crash dump becomes readable |
| 9.3 gdb-remote stub | kgdb/`gdbstub`: RSP `?`/`g`/`G`/`m`/`s`/`c` over serial | On top of 9.1's serial RX: `kernel/console/gdbstub.cc` — trap on #DB/#BP, serve registers/memory, resume. Bigger but mechanical once a monitor exists | L | Step-debug drivers on real hw |
| 9.4 Static probe sites (carry-over) | Linux tracepoints / DTrace SDT | `KPROBE(name)` macro recording into the existing `guard/trace.h`-style rings; sprinkle at sched/vfs/net/guard hot paths | S | Cheap introspection (dynamic INT3 patching stays deferred) |

## 10. Power

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 10.1 S3 suspend/resume | Linux `suspend` path: device suspend→resume protocol, `\_S3` package, PM1 wake events, firmware waking vector (FADT `FIRMWARE_WAKING_VECTOR`), non-boot-CPU re-park, memory context preserved | `kernel/acpi/fadt.cc` decodes `_S5` only (line 161+) — extend AML walk to `\_S3`; `kernel/power.cc` gains `power_suspend()`: freeze sched threads, per-driver `suspend`/`resume` hooks (block/net/audio/xhci), save+restore GDT/IDT/CR3 context, jump waking vector. Much harder than S5 — S5 throws state away, S3 must preserve it | XL | The laptop feature; highest wow/effort ratio *after* the cheap wins |

## 11. Security

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 11.1 pledge-style syscall filtering | SerenityOS `pledge()` (OpenBSD model): per-process mask of syscall groups, monotonically reducing; `unveil` ≈ our landlock (already have) | `kernel/syscall.cc` table exists — add a per-app `promise_mask` (thread/app attribute) checked in `syscall_dispatch` + HypeGuard `GuardOp`; landlock (`kernel/guard/landlock.h`) already covers the fs-path half | S/M | Sandbox depth for .hxer/.hxbc apps, cheap |
| 11.2 Kernel ASLR | Linux KASLR: slide the kernel image at boot, patch relocations; needs a relocatable kernel or a pre-computed fixup table | `kernel/linker.ld` fixes all addresses today; simplest path = boot-time pick of N slide slots + a `.rela`-style fixup table emitted at build; RDRAND (`kernel/rng/entropy.cc`) supplies the pick | L | Real exploit mitigation; mostly build plumbing |
| 11.3 unveil-parity check | landlock already ≈ OpenBSD unveil — keep parity | `kernel/guard/landlock.h` — done; extend ops to non-fs GuardOps as they appear (net bind/connect classes) | S | Keep the model honest |

## 12. Time

| Capability | What real OSes do | Hook point in hypeaios | Effort | Value |
|---|---|---|---|---|
| 12.1 Unified wall clock (RTC base + NTP correction) | Linux `clock_gettime(CLOCK_REALTIME)` = boot RTC base + periodic NTP discipline (adjtimex); our RTC and NTP each exist but never meet — deskbar/lockscr read raw `rtc_now()` while `net_time_unix()` lives in a separate universe | New `kernel/time/wallclock.{h,cc}`: `utc_now()` = max(RTC base + uptime, ntp offset + uptime) with monotonic guard; repoint `deskbar.cc:647`, `lockscr.cc:96`, `hxshell.cc:883` to it; optional `rtc_write()` on clean shutdown | S | One truthful clock everywhere |
| 12.2 RTC write-back | OSes sync hw clock on shutdown/time-set | `kernel/time/rtc.cc`: add `rtc_set(RtcTime)` (ports 0x70/0x71 already read); call from `power_shutdown` when NTP-synced | S | Correct time across reboots |

## 13. Architectural options to consider later (not near-term)

- **Userspace services/schemes (Redox model)** — `svc/revsvc.h` registry +
  VFS-mountable scheme paths (`net:/`, `audio:/`) would let drivers move
  out of ring 0 once userspace exists. Direction-setting, not a ticket.
- **Async syscall completion ring (Managarm/io_uring)** — SQE/CQE shared
  ring; revisit after 5.x IPC is real.
- **Kernel modules (.hxm)** — post-boot native-code load; still deferred:
  hxbvm covers the "loadable code" need with zero relocation complexity.
- **Kernel net server (telnet/HTTP status daemon)** — trivial once 3.1+3.2
  land; nice demo, low priority.

---

## Top 10 (dependency-respecting, value-per-effort)

1. **Userspace (1.1→1.4, XL phased).** Ring-3+TSS+GDT → address spaces →
   syscall ABI → ELF .hxer. Still *the* flagship gap — everything above it
   in value terms de-risks it. `arch/x86/` gains gdt/tss/syscall_entry;
   `memory/vmm.cc` per-space CR3; `syscall.cc` table grows;
   `hxrt/elf.cc` + `hxpack.cc`.
2. **Event-driven blocking in sched (6.1, S/M).** Wire `wait_queue` into
   `sched::block_on`/`wake_*`. Threads today can only sleep on *time*;
   this is the cheap prerequisite for real IPC, blocking sockets and
   driver threads. Fold in the locking audit (6.2) as threads multiply.
3. **IPC: message ports + pipes + shm (5.1–5.3, M).** The BeOS-shaped
   pieces XAU already proves; turns apps into a system and gives the
   window server, clipboard and services a real substrate.
4. **Kernel debugger trio: serial RX + monitor + map-symbolized
   backtraces (9.1–9.2, S/M).** Serial is TX-only today; panic dumps rbp
   but can't name it. Biggest debuggability-per-LoC on this list; every
   later item gets easier.
5. **Demand-paging fault path (2.1, M→L).** `vmm_page_fault` consulted
   before `panic_exception` — lazy stacks, mmap regions (2.3), and the
   honest footing swap (2.2) needs once address spaces exist.
6. **Socket fd API + HTTP client (3.1–3.2, M).** TCP/DNS exist; make them
   files and fetch a URL. Highest-visibility net feature per effort.
7. **TLS 1.3 + X.509 (3.3, L).** Primitives in tree (sha256/ecdsa/rdrand);
   X25519+ChaCha20-Poly1305+SHA-256-only suites keep it tractable →
   HTTPS update channel.
8. **virtio-1.0 modern transport (8.1, M).** Both gpu and net currently
   skip modern-only devices; one shared transport fixes all virtio
   devices at once and enables MSI-X virtqueues.
9. **pledge-style syscall filter (11.1, S/M).** Syscall table + landlock
   already exist; a per-app promise mask is the SerenityOS `pledge()` to
   our `unveil()` — cheap security depth for .hxer/.hxbc apps.
10. **Unified wall clock (12.1, S).** RTC base + NTP offset behind
    `time::utc_now()`, repoint the three UI consumers, write RTC on clean
    shutdown. Cheapest real correctness win.

Honorable mentions: 5.5 event queue + 5.6 futex (M, ride on #2/#3);
8.2 runtime modeset (M); 8.4 kernlets on hxbvm (M, distinctive);
4.4 pseudo-fs (S/M); 7.x touchpad+IME (M, real-hw appeal); 1.5 per-task
fault kill (M, part of the userspace phase); 10.1 S3 (XL, post-#1);
11.2 KASLR (L, build plumbing); 3.5 IPv6 (L, low urgency under slirp);
4.1 journaling (M, matters on real hw); 4.2 ext2 read (M, interop).

---

## Inspected (repo evidence)

Files read/grepped this scan: `kernel/sched/{thread,scheduler}.{h,cc}`,
`kernel/interrupts/idt.cc` (exception→panic path, vector registry),
`kernel/syscall.{h,cc}` (10-slot dead table), `kernel/arch/x86/` (no
gdt/tss/syscall_entry files), `kernel/memory/{vmm,pmm}.{h,cc}` (huge-page
translate-only, no fault path), `kernel/net/{net,tcp,ntp,virtio_net,
e1000}.{h,cc}` (functional TCP API, poll-driven virtio RX, IRQ'd e1000),
`kernel/vfs/{fdtable,hypefs,hypeblob,fat32}.{h,cc}` (file-only fd kinds),
`kernel/xdp/{xdp.h,backend_virtgpu.cc}` (legacy-only transport, EDID
negotiated, modern-only skipped at :395), `kernel/display/vgadispi.cc`
(`dispi_set_mode`), `kernel/acpi/fadt.cc` (`_S5` only), `kernel/power.cc`,
`kernel/time/rtc.{h,cc}` + `kernel/net/ntp.h` (unreconciled clocks),
`kernel/guard/{landlock,guardapi,trace}.h`, `kernel/hxbvm/`,
`kernel/log/{crashdump,kring}`, `kernel/panic.cc`, `kernel/console/fbcon`,
`kernel/serial.{cc,hpp}` (TX-only), `kernel/win/xau.h` (posted-msg FIFO),
`kernel/input/{msaux,scancode,hypeptr,keyring}`, `kernel/kvm/`,
`kernel/linker.ld`, `tools/mkimage.py` (kernel.map reader),
`kernel/main.cc` (idle-loop pumps at ~1156–1199), `kernel/sched/watchdog.cc`.

Docs read: `docs/research/gaps-kernel.md` (full — stale items re-based),
`docs/research/gaps-hardware.md`, `docs/research/gaps-desktop.md`,
`docs/research/core-os-improve.md`, `docs/research/os-core-roadmap.md`.

Recent commits inspected: `6b5a3c4` (research: HypeUI design system),
`5655ef1` (merge), `a0988b6` (SDHCI, #141), `bbe8574` (UAC, #142).

External sources: SerenityOS man pages (man2 syscall list, pledge/unveil,
Mitigations(7)); Managarm Handbook (posix subsystem, mbus, thor/eir) +
FOSDEM'22/FG-BS'24 papers (async syscalls, hel streams, kernlets);
ToaruOS 2.0 release notes + Misaka/Yutani design (shm window buffers,
pex pipes); Redox Book (schemes, event scheme, ipcd shm/chan, relibc);
skiftOS FOSDEM'26 slides + hjert commits (RPC ports, mmap-able clock).
