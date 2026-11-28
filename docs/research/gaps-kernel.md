# HypeAI OS — Kernel/Subsystem Gap Audit vs Mature Hobby OSes

Scope: bare-metal x86-64 kernel. What mature hobby OSes (osdev.wiki
"Required Features" / "Going Further" tracks), Linux/FreeBSD internals and
POSIX all have that we still lack. Every entry names the canonical spec or
reference implementation, the actual file(s) in this tree it would plug
into, a rough effort grade and the value it buys.

Effort grades (one engineer who knows the subsystem):
**S** < 1 day / < ~300 LoC — **M** 1–3 days / ~300–800 LoC —
**L** ~1–2 weeks / ~800–2500 LoC — **XL** multi-week / 2500+ LoC.

Brother docs: `docs/research/os-core-roadmap.md` (spec index),
`docs/research/core-os-improve.md` (fix-ups inside existing subsystems),
`docs/kernel-architecture.md`.

---

## 0. Current architecture snapshot (ground truth)

- **Scheduler** — `kernel/sched/scheduler.cc` is a *selftest stub*: two
  statically allocated 4 KiB stacks, a cooperative
  `hype_context_switch` (kernel/arch/x86/context.asm saves callee-saved
  regs only), `scheduler_selftest()` runs both workers to completion.
  There is no task table, no run queue, no preemption, no sleep/wake.
  `kernel/lmhtask.h` defines an inert `Task` state machine
  (Ready/Running/Waiting/Sleeping/Zombie) and `kernel/thread.h` an
  unused `ThreadInfo` + priority-class → quantum mapper. Nothing calls
  them.
- **SMP** — `kernel/arch/x86/smp.cc` does full MADT → INIT-SIPI-SIPI
  bring-up; APs end in `ap_park_loop()` (`hlt` + counter, smp.cc:231).
  Every CPU runs its LAPIC timer on vector 32 at 100 Hz
  (`idt.cc:timer_handler` ticks on BSP only). So we have N online CPUs
  executing zero work — the scheduler's per-CPU queues have hardware
  waiting for them.
- **Locking** — `kernel/spinlock.cc`, `mutex.cc`, `semaphore.cc`,
  `wait_queue.cc` exist as *dead primitives*: zero call sites outside
  their own files (verified by grep). With APs live, every shared
  structure is a latent race.
- **Userspace** — none. Bootloader GDT has kernel segments only
  (selectors 0x08/0x10/0x18 per `ap_tramp.asm` comments); no TSS, no
  user segments, no `syscall` MSRs. `kernel/syscall.cc` is a 10-slot
  handler table never invoked by a real trap.
- **Memory** — PMM bitmap over 128 MiB (`kernel/memory/pmm.h`),
  identity-mapped VMM < 4 GiB with map/unmap (`kernel/memory/vmm.h`),
  `vmm_user` flag exists but is unused. Heap over the heap region.
- **Apps** — `.hxer` = compile-time `HxerHeader` struct with raw C++
  function pointers (`kernel/hxrt/hxer.h`); `hxpack.cc` serializes the
  header but deserialized blobs have null entry points — there is no
  loader. Everything runs in ring 0.
- **Net** — `kernel/net/net.cc`: poll-driven eth+ARP(8)+ICMP echo
  *reply*+UDP-lite+DHCP client on virtio-net. `net_poll()` is called
  from the idle loop (`main.cc:1081`), same loop that pumps
  `hasio_pump_active`, `runnervx_pump`, `fw_poll`, `fs_sync`
  (main.cc:1040–1095) — a cooperative mega-loop, not threads.
- **VFS** — memfs with S:/U:/T: disks (`kernel/vfs/hypefs.h`), fd table
  with generation-checked slots (`fdtable.h`), FAT32 read path
  (`fat32.cc`), persistence via HYPEDATA blob at fixed LBA 4129
  (`hypeblob.cc`) written through ATA PIO/AHCI DMA (`kernel/block/`).
- **Security** — HypeGuard LSM-style hook chain + landlock fs rulesets
  (`kernel/guard/`); capability bits in `kernel/apgsecurtiy.h`; ECDSA +
  SHA-256 + RDRAND RNG already in `hcpp/`.
- **Misc already present** — RevServices registry (`kernel/svc/revsvc.h`,
  incl. tiny KV `svc_data_set/get`), RunnerVX command lanes
  (`kernel/runner/`), XAU Win32-style message queues
  (`kernel/win/xau.h`), XDP compositor, xHCI (`kernel/usb/xhci.cc`),
  RTC (`kernel/time/rtc.cc`), SQLite freestanding port
  (`third_party/sqlite`, wrapped by `kernel/db/hypedb.hpp`).

---

## 1. Execution model

### 1.1 Real preemptive SMP scheduler

- **What it is.** Per-CPU run queues, priority-based pick-next, timer
  preemption on every LAPIC tick, sleep/wake states, idle task per CPU,
  rescheduling IPI. The reference shape: a small O(1)-ish bitmap+queue
  scheduler like Linux 2.6's runqueue, or FreeBSD ULE's per-CPU
  `tdq` — both overkill; the hobby-OS canonical is a priority round-
  robin array of queues (osdev "Processes and Threads" / Brendan's
  multitasking tutorial).
- **Current depth.** `scheduler.cc` proves only that
  `hype_context_switch` works. Missing: a task struct owning kernel
  stack + saved rsp + state + cpu affinity; per-CPU `current`; tick
  hook; block/unblock; wake queues; idle loop = schedule-next-or-hlt.
  `lmh::Task` (lmhtask.h) already has the right state enum — extend it
  rather than invent a parallel type.
- **Hook points.**
  `kernel/sched/scheduler.cc` — rewrite core: `Task` table (say 128
  slots), `ready_bitmap[prio]` + `run_queue[prio]` per CPU.
  `kernel/sched/context.cc` — keep `hype_context_switch`, add
  `sched_switch_frame` that also swaps a future CR3/TSS.rsp0.
  `kernel/interrupts/idt.cc:timer_handler` — call
  `sched_tick(smp_cpu_id())` on *every* CPU (today BSP-only tick
  counting stays) → decrement quantum, set need_resched.
  `kernel/arch/x86/smp.cc` — replace `ap_park_loop()` with
  `sched_idle()`; `send_ipi()` is `static` — export as `smp_send_ipi()`
  + add a reschedule vector (e.g. 0xFD) registered via
  `idt_vector_register`.
  `kernel/wait_queue.h` — becomes real: `task_sleep_on(queue)`,
  `wake_one/all` (the existing u32-task-id list fits).
  `kernel/thread.h` — `thread_apply_priority` maps onto real quanta.
  `kernel/main.cc` idle loop — becomes kernel thread 0 on BSP.
- **Effort:** L. **Value:** foundational — unlocks 1.2, 3.x, 4.x, IPC,
  event model, hung-task detection. The single highest-leverage gap.

### 1.2 Kernel threads for drivers/services

- **What it is.** `kthread_create(fn, arg, name)` — the standard Linux
  pattern (`kthread_run`, `kthread_should_stop`). Turns the idle-loop
  pump calls into real blocking threads.
- **Current state.** `main.cc:1040–1095` cooperatively pumps:
  `cursor_poll`, `net_poll`, `hasio_pump_active`, `runnervx_pump`,
  `fw_poll`, `fwalsa_pump`, `lmpu401_poll`, `fs_sync`. Any one of them
  stalling stalls everything (audio pump already had to be made
  explicitly non-blocking — see comment).
- **Hook points.** New `kernel/sched/kthread.{h,cc}` on top of 1.1;
  move `net_poll` → `netd` thread blocked on virtio RX kick (see 1.3);
  `hasio`/`fw`/`runnervx` → their own threads sleeping on wait queues;
  `fs_sync` → `ksyncd` woken by dirty flag + debounce timer. RevService
  `svc_start` (revsvc.cc) gets a `svc_flags svc_runs_thread` mode.
- **Effort:** M once 1.1 lands. **Value:** kills the cooperative
  mega-loop; a wedged driver no longer wedges the desktop.

### 1.3 Interrupt-driven RX path (prerequisite for threads)

- Not strictly a "subsystem", but `virtio_net` acknowledges RX IRQs and
  still delivers via `virtio_net_recv()` polling (`virtio_net.h`
  comment). Wiring the RX virtqueue's used-event to a wait queue + a
  dedicated MSI/MSI-X vector through `idt_vector_register` (idt.cc:81)
  turns netd into an event-driven thread.
- **Hook:** `kernel/net/virtio_net.cc`, `idt.cc`, `wait_queue.h`.
- **Effort:** S/M. **Value:** latency + the template for IRQ→thread
  wakeup used by AHCI/xHCI next.

### 1.4 Userspace: ELF64 .hxer, address spaces, syscall ABI, ring-3

The "make the kernel universal" ask. Four inseparable pieces:

1. **GDT + TSS.** New `kernel/arch/x86/gdt.{asm,cc}` (kernel owns its
   GDT instead of inheriting stage2's — see `ap_tramp.asm` mirroring
   note) with user code/data segments + `kernel/arch/x86/tss.{h,cc}`
   per-CPU TSS (rsp0 for syscall/interrupt stack switch, IST for #DF
   and NMI — needed regardless for crash robustness).
   Ref: Intel SDM Vol 3A §6–7; osdev "GDT", "TSS", "Getting to Ring 3".
2. **Separate address spaces.** `kernel/memory/vmm.cc`: add
   `vmm_create_space()` (new PML4 with kernel half shared — since we're
   identity-mapped < 4 GiB, map user regions in the low canonical half
   and keep kernel physmap high or via a separate region; simplest
   scheme: clone kernel PML4 entries ≥ some split, user pages below).
   Per-task CR3 in `sched::Task`; `hype_context_switch` loads it.
   Add per-CPU TLB shootdown IPI on `vmm_unmap_page` (reuse 1.1's IPI).
3. **Syscall ABI.** `syscall`/`sysret` MSRs (STAR/LSTAR/SFMASK) — new
   `kernel/arch/x86/syscall_entry.asm` + dispatch onto the existing
   `kernel/syscall.cc` table (grow past 10 slots). `int 0x80` as a
   fallback gate. ABI: SysV arg regs, nr in rax — same shape as Linux
   so ported knowledge applies. HypeGuard checks (`guard_check`)
   re-anchor here: subject = current task instead of ambient app name.
4. **ELF64 loader.** `kernel/hxrt/elf.cc`: parse ELF64 ET_EXEC/ET_DYN
   per the SysV gABI, PT_LOAD → `vmm_map_page(user=true)` + PMM frames,
   entry → user-mode iret frame. `.hxer` becomes a packaging format
   around ELF (hxpack already has a header+path shape — embed the ELF
   blob after `HxpackHeader`). Apps live on `U:/apps/*.hxer` loaded via
   `fs_open`/`fs_fread` (fdtable.h) instead of compiled in.
   `HxerHeader.entry` fn-pointer model is replaced by "start a process".
   Ref: ELF spec <https://refspecs.linuxfoundation.org/elf/elf.pdf>;
   osdev "ELF", "User Space"; Linux `binfmt_elf.c`.
- **Effort:** XL (biggest item here; realistically split into 1.4a
  ring-3+TSS+GDT (L), 1.4b address spaces (L), 1.4c syscall ABI (M),
  1.4d ELF loader (M)). **Value:** the ask itself — memory protection,
  crashable apps, a real ecosystem boundary. Everything else (libc,
  IPC-between-apps, per-task crash recovery) builds on it.

### 1.5 Per-task exception recovery (no setjmp)

- **What it is.** Today every CPU exception funnels to
  `panic_exception` (idt.cc:67) → whole-machine halt. Mature kernels
  kill only the faulting context: on #PF/#GP in user mode → terminate
  task; in kernel mode on a task stack → task Failed + reschedule;
  only fault in idle/ISR context stays fatal.
- **Hook points.** `interrupt_handle` exception branch (idt.cc) → ask
  `sched::current()`; if a task context, mark `TaskState::Failed`,
  record rip/cr2 into the task struct, `sched_yield`. Needs the TSS
  IST stack for #DF and NMI-watchdog (1.4a). Until userspace exists,
  deliver this for kernel threads first — a bad driver stops panicking
  the box.
- **Ref:** Linux `do_page_fault`/`die()` split; osdev "Exceptions".
- **Effort:** M after 1.1, L standalone. **Value:** reliability
  multiplication; pairs with crash dumps (6.1).

---

## 2. SMP safety — locking audit

- **What it is.** APs currently only `hlt`, so no real concurrency yet —
  but the instant 1.1/1.2 lands, every shared structure races. Audit
  the subsystems that will run on multiple CPUs.
- **Primitives we already have** (dead code today): `kernel/spinlock.h`
  (test-and-set + RAII guard), `mutex.h` (recursive, owner-tagged),
  `semaphore.h`, `wait_queue.h`, plus `__atomic_*` builtins used in
  `smp.cc`. Missing piece: an IRQ-save spinlock variant
  (`spin_lock_irqsave`) — required before any lock is taken from both
  thread and ISR context.
- **Audit list (who needs what):**
  - `kernel/memory/pmm.cc` bitmap allocator — **spinlock** (first
    lock; everything allocates).
  - `kernel/memory/heap.cc` — spinlock or per-CPU caches.
  - `kernel/vfs/hypefs.cc` + `fdtable.cc` + `hypeblob.cc` — global
    memfs mutex (writer-serializing is fine); `hypeblob_io_active`
    flag is already a manual exclusion hack.
  - `kernel/audio/pullmix.cc` — source list + ring buffers; SPSC rings
    per source (lock-free) + spinlock on the source table.
  - `kernel/net/net.cc` — ARP table + UDP bind table + DHCP state:
    one net mutex; virtio rings need their own TX/RX locks.
  - `kernel/win/winman.cc` + `xau.cc` — a single GUI lock (classic
    "big window lock" — Windows/BeOS did this; fine forever).
  - `kernel/svc/revsvc.cc`, `kernel/hxrt/hxrt.cc`,
    `kernel/guard/guard.cc` rule/trace rings — small spinlocks.
  - `klog.h` — the serial write path and the `sink` pointer are
    unprotected; concurrent klog from 2 CPUs interleaves bytes today
    (already true: AP timer IRQs could klog). **S-fix first.**
  - `kernel/sched/` internals — run-queue locks are per-CPU + a global
    task-table lock; keep the protocol "lock your own queue first".
- **Ref:** osdev "Spinlocks"; Linux `Documentation/locking/`; FreeBSD
  `mutex(9)`.
- **Effort:** M (audit + sprinkle, mostly mechanical once scheduler
  semantics exist). **Value:** correctness gate — must land with 1.1,
  not after.

---

## 3. Memory management beyond identity-map

### 3.1 Demand paging + swap + OOM killer

- **What it is.** Not-present page faults allocate frames lazily (stack
  growth, mmap-style file views); a swap partition/file reclaims cold
  pages; when `pmm_allocate_page` fails, an OOM killer shoots the
  fattest task.
- **Current base.** `vmm_map_page`/`vmm_unmap_page` work on one CR3;
  `#PF` always panics (idt.cc → panic_exception → panic.cc:164);
  PMM is a 128 MiB bitmap — tiny, so an OOM policy actually matters.
  Block I/O exists (`kernel/block/blockdev.h`, ahci.cc/ata.cc) — a swap
  region can live at a fixed LBA like hypeblob does.
- **Hook points.** `kernel/memory/vmm.cc`: `vmm_page_fault(cr2,
  error)` consulted *before* `panic_exception` in idt.cc; not-present
  PTE encoding (swap slot in PTE bits while P=0) per SDM. New
  `kernel/memory/swap.{h,cc}` (slot allocator over a disk LBA range)
  and `kernel/memory/oom.{h,cc}` (task RSS accounting → victim pick,
  needs 1.1's task table). Anonymous demand-zero pages come free once
  the fault path exists.
- **Ref:** osdev "Page Frame Allocation"/"Paging"; Linux `mm/oom_kill.c`,
  `mm/memory.c` fault path; FreeBSD `vm_fault`.
- **Effort:** XL end-to-end (fault path alone = M, swap = L, OOM = S).
  **Value:** turns the 128 MiB PMM ceiling into a soft limit and gives
  every later subsystem lazy-allocation for free.

---

## 4. Networking

### 4.1 TCP stack

- **What it is.** RFC 9293 (obsoletes 793) state machine: TCB per
  connection, 3-way handshake, seq/ack windows, retransmit timer,
  minimal congestion (or fixed window first). Hobby reference: osdev
  "TCP", lwIP `tcp.c`/`tcp_in.c`/`tcp_out.c` as the compact-model
  archetype.
- **Hook points.** New `kernel/net/tcp.{h,cc}` beside `net.cc`. Reuse:
  `inet_checksum` (net.cc, static — factor into a `netutil.h`),
  `net_arp_resolve`, `virtio_net_send`. Deliveries land on the same
  poll/thread as UDP (upgrade to event-driven with 1.3). Socket
  layer: extend `kernel/vfs/fdtable.cc` with `fd_kind::Socket` so
  `fs_fread/fs_fwrite` work on TCP fds — the POSIX shape — plus
  `net_socket/bind/listen/accept/connect` syscalls once 1.4c exists.
  Retransmit uses `time::uptime_ns()` timers in the netd thread.
- **Effort:** L. **Value:** opens DNS-over-TCP, HTTP/HTTPS fetch,
  a real net service; highest-visibility net feature.

### 4.2 DNS resolver

- **What it is.** RFC 1035 wire format over UDP 53: build query,
  parse response with **name compression** (the classic bug source —
  bounded pointer-walk), small TTL cache, `net_resolve(name)`.
- **Hook points.** New `kernel/net/dns.cc`; needs a DNS server IP —
  extend the DHCP option parser in `net.cc` (`dhcp_option()` already
  exists, add option 6 + option 3 router it already takes). Surface in
  a `hxnet` app / RevDBG pane.
- **Effort:** S. **Value:** makes every other net feature usable
  (NTP, HTTPS, update fetch). Do early.

### 4.3 NTP time sync

- **What it is.** SNTP client, UDP 123, RFC 5905: 48-byte request,
  apply server offset as an adjustment — do **not** write the RTC;
  keep `time::uptime_ns()` monotonic and add `time::utc_now_ns()` =
  uptime + learned offset + RTC base.
- **Hook points.** `kernel/net/ntp.cc` (uses 4.2 to reach
  `pool.ntp.org` or the DHCP NTP option 42); `kernel/time/timer.cc`/
  `rtc.cc` for the offset store; RevDBG pane shows skew.
- **Effort:** S. **Value:** correct timestamps for TLS cert checks
  and logs — near-free on the UDP base.

### 4.4 TLS 1.3 + X.509/HTTPS feasibility

- **What it is.** Minimal TLS 1.3 client: X25519 key exchange,
  ChaCha20-Poly1305 AEAD (freestanding-friendly — pure integer ops, no
  AES-NI dependency), HKDF over SHA-256, certificate-verify via
  X.509 DER parse + ECDSA-P256/RSA-2048 verify.
- **Current base.** Already in tree: `hcpp/sha256.hpp` (HKDF core),
  `hcpp/ecdsa.hpp` (ECDSA verify — the X.509 sig-check primitive),
  `hcpp/rng.hpp` with **RDRAND backend already working**
  (`rng_backend()`); TCP from 4.1. Missing: chacha20/poly1305/x25519
  (≈600 LoC of well-specified crypto, RFC 8439 + RFC 7748) and a DER
  parser (~500 LoC) + a root-cert bundle shipped as `S:/data/certs.bin`
  through hypeblob (a dozen roots is enough for a hobby OS).
- **Hook points.** `hcpp/chacha20.hpp`, `hcpp/poly1305.hpp`,
  `hcpp/x25519.hpp`; `kernel/net/tls.{h,cc}` (record layer,
  handshake state machine); `kernel/net/x509.cc`; trust store file
  on the S: disk. Honest effort: getting *correct enough* TLS is the
  hard part — X25519+ChaCha20-Poly1305+SHA-256-only cipher suites make
  it tractable.
- **Ref:** RFC 8446 (TLS 1.3), RFC 8439, RFC 7748, RFC 5280 (X.509);
  BearSSL as the small-footprint reference.
- **Effort:** L (XL if RSA verify + full PKIX chain building are in
  scope). **Value:** HTTP-over-TLS fetch → update channel, package
  pulls; big credibility feature.

### 4.5 ping + netstat (net observability)

- **What it is.** ICMP echo *request* path (we only answer today —
  `icmp_reply` at net.cc:275) with a pending-echo table + RTT
  measurement; a netstat view: ARP cache, UDP binds, TCP TCBs once 4.1
  exists, DHCP lease.
- **Hook points.** `net.cc` add `icmp_echo_send`/`ping` pending table;
  new app `kernel/hxrt/apps/hxping.cc`; netstat pane inside
  `kernel/hxrt/apps/revdbg.cc` (the RevDBG window already auto-
  refreshes from the idle loop, main.cc:1044).
- **Effort:** S. **Value:** debuggability of the whole net stack;
  cheapest feature on this list.

---

## 5. Application ecosystem

### 5.1 Bytecode VM / JIT for .hxer (real app format)

- **What it is.** `.hxer` today = a header with raw C++ fn pointers —
  no code can be loaded at runtime at all (hxpack.cc's comment:
  "Deserialized blobs leave it null"). Two paths:
  (a) **HXB bytecode VM** — a small register-based ISA (~30 ops: ALU,
  call, sys-call gate, canvas/input builtins), interpreter in
  `kernel/hxrt/hxbvm.cc`, assembler/compiler as a host tool in
  `tools/`; (b) real machine-code loading — that *is* 5.4/1.4d.
  Recommended: (a) now (portable, sandboxable, tiny), (b) later via ELF.
- **Hook points.** `kernel/hxrt/` — `hxbvm.cc` interpreter +
  `HxerHeader` gains `kind=Bytecode` with `entry` pointing at the VM
  trampoline; `hxpack.cc` carries the bytecode blob; HypeGuard
  (`kernel/guard/`) checks apply at the VM's syscall-gate op — a
  natural security boundary ring 0 can't give native code.
- **Ref:** osdev "Bytecode interpreters"; Lua 5.x VM design (tiny,
  proven); WebAssembly MVP spec if we want a standardized target.
- **Effort:** L (interpreter M + toolchain M). **Value:** the
  difference between "apps compiled into the kernel" and "an app
  ecosystem". A JIT tier on top is a separate XL.

### 5.2 Kernel module loading

- **What it is.** Post-boot driver/service load from `U:/` — the
  driver model's missing half. Two formats: (a) ELF64 REL object +
  a kernel export table (kallsyms-lite); (b) our own `.hxm` flat blob
  with an import-name→address fixup table — much easier and adequate.
  Signed modules via `hcpp/ecdsa.hpp` (key baked into kernel) close
  the loop with HypeGuard.
- **Hook points.** `kernel/hxrt/modload.cc`; `fs_open`/`fs_fread`
  (fdtable.h) for the file; `svc_register` (revsvc.h) so modules appear
  as services; relocation fixups over a static export table generated
  from the linker map.
- **Ref:** Linux `kernel/module/` + `insmod`; FreeBSD `kldload`; osdev
  "Modules".
- **Effort:** L. **Value:** driver dev without full rebuilds; on the
  critical path to "universal kernel".

### 5.3 IPC: pipes + shared memory between apps

- **What it is.** POSIX-adjacent: anonymous/named pipes (bounded ring
  buffers, blocking read/write) and named shared-memory objects.
- **Current base.** XAU already has a posted-message queue per window
  (`xau_post`, 64-entry FIFO, kernel/win/xau.h) — the proven in-tree
  message-passing primitive to generalize. `wait_queue.h` gives the
  block/wake. HypeGuard `GuardOp` gains `Ipc` for policy on cross-app
  channels.
- **Hook points.** New `kernel/ipc/pipe.cc` (ring + wait queues +
  `fd_kind::Pipe` in fdtable so `fs_fread`/`fs_fwrite` just work) and
  `kernel/ipc/shm.cc` (until 1.4b, shm = guarded shared heap buffers;
  after, real shared pages via `vmm_map_page` into both CR3s). Named
  pipes mount as `U:/pipes/<name>` — memfs already supports it.
- **Ref:** POSIX `pipe(2)`/`shm_open`; BeOS ports (closest cultural
  match — XAU is already BeOS-flavored).
- **Effort:** M. **Value:** turns apps into a system instead of
  islands; window-server, clipboard, service RPC all ride on it.

### 5.4 epoll-like event source model

- **What it is.** One wait-object aggregating fd-readiness (pipes,
  sockets, files), timers and GUI events — `epoll`/`kqueue` shape.
- **Current base.** XAU timers + message queue already multiplex the
  GUI side (`xau_set_timer`, `xau_pump`); `fdtable` gives fd objects to
  hang readiness on; net sockets become pollable once TCP exists.
- **Hook points.** `kernel/event/evq.{h,cc}`: `evq_create`,
  `evq_add(fd|timer|xau_win, mask)`, `evq_wait(timeout)` →
  `task_sleep_on` a wait queue until 1.1 makes it genuinely blocking
  (before that, `evq_poll` nonblocking still unifies the API).
- **Ref:** Linux `epoll(7)`, FreeBSD `kqueue(2)`.
- **Effort:** M. **Value:** the single API apps use to not-busy-wait;
  pairs with 1.2/4.1.

### 5.5 POSIX-ish libc subset (libhype)

- **What it is.** Not a full libc — the hobby-OS slice: `string.h`
  complete, `malloc/free` over a per-process heap syscall, minimal
  `stdio` (`printf`→serial, `fopen/fread`→fdtable), `errno`, `unistd.h`
  wrappers over the syscall ABI.
- **Gap vs hcpp.** `hcpp/` is kernel-internal C++: types.hpp,
  `string_view`, `text_writer`, `memory.hpp`, sha256/ecdsa/rng — it is
  **not** a C ABI: no `memcpy`-complete `string.h` surface, no errno
  model, no FILE, no syscall wrappers, and it compiles freestanding-
  only into kernel objects.
- **Hook points.** New `userland/libhype/` (C, syscall-stubbed via
  `syscall` insn from 1.4c); headers describing the 1.4 ABI.
- **Ref:** POSIX.1-2017 / Issue 8; musl as the readable reference
  (do not import, license-wise we stay all-own-code per roadmap).
- **Effort:** L (endless tail; MVP subset M). **Value:** required
  before any third-party-ish port; the ecosystem on-ramp.

---

## 6. Reliability, observability, persistence

### 6.1 Kernel crash dumps

- **What it is.** On panic, persist the fault record so post-mortem
  works after reboot — the `kdump`/minidump idea in miniature.
- **Current base.** `panic.cc` writes registers to serial + blue
  screen — lost on power-off. `klog` writes straight to serial with a
  `sink` tee (klog.h) — **there is no in-memory log ring**; adding a
  4–8 KiB ring sink is trivial. Persistence target exists: `fs_write`
  to `U:/crash.log` when VFS is healthy, else a **raw sector dump to
  the hypeblob region's tail via ATA PIO** (ata.cc — polling mode, no
  IRQ/DMA needed, exactly what a panic path wants). panic must stay
  allocation-free: write the ring + the `InterruptRegisters` struct,
  then halt.
- **Hook points.** `kernel/log/kring.cc` (new ring, wired as klog
  sink #2); `panic.cc` → `crash_dump_write()`; boot banner in `main.cc`
  reads back `U:/crash.log` presence → "last boot crashed" RevDBG
  entry.
- **Effort:** S. **Value:** outsized — every field crash becomes
  debuggable; do early.

### 6.2 Watchdog timer + hung-task detector

- **What it is.** Two levels: (a) soft hung-task detector — each CPU's
  scheduler tick stamps `last_progress`; a monitor (BSP timer context)
  flags tasks/CPUs that missed N ticks → klog + optional task kill;
  (b) hardware watchdog — Intel TCO (ICH datasheet) or QEMU `ib700` —
  reboots a dead box.
- **Current base.** `SmpCpu.counter` already counts AP progress
  (smp.cc) — the detector is "BSP ticks minus AP counter > threshold".
  NMI delivery via LAPIC LVT gives a true NMI watchdog for hard hangs.
- **Hook points.** `kernel/sched/watchdog.cc`; `idt.cc` timer path for
  the stamp; `smp.cc` counters; optional `kernel/acpi/` TCO port I/O.
- **Ref:** Linux `kernel/watchdog.c` + `hung_task.c`; Intel ICH TCO
  spec.
- **Effort:** S for the soft detector, M for TCO/NMI. **Value:** field
  robustness + pairs with 6.1 to catch hangs, not just crashes.

### 6.3 DTrace-like probes

- **What it is.** Named probe sites with attachable callbacks —
  `PROBE("vfs:write", path, len)` — off = a skipped call-site, on =
  record into a ring/consumers.
- **Current base.** `klog` markers are static strings; HypeGuard's
  `gcerd`/`trace` rings (kernel/guard/gcerd.h, trace.h) already
  implement the "record-into-ring + serial mirror" machinery to copy.
  Two tiers: **static sites** (compile-time table, zero-ish cost) and
  **dynamic** (INT3 patch over a NOP5 at function entry — needs
  SMP-safe text patching via resched-IPI freeze; that's the DTrace
  FBT model).
- **Hook points.** `kernel/dbg/probe.{h,cc}` table + consumer ring;
  macro `KPROBE(name, args...)` sprinkled at vfs/net/sched/guard hot
  points; RevDBG pane to arm/read probes.
- **Ref:** DTrace (SDT/FBT), Linux tracepoints/`trace_events`.
- **Effort:** S for static sites, L for dynamic patching. **Value:**
  makes the kernel introspectable without rebuilds; force-multiplier
  on every later subsystem.

### 6.4 Framebuffer console mode

- **What it is.** A text shell straight on the framebuffer — no
  desktop — for rescue/debug/headless-ish use; the Linux VT/FreeBSD
  `vt(4)` analogue.
- **Current base.** `panic_screen` (panic.cc:68) already renders text
  via `hcpp/graphics/draw_text` + `hcpp/fonts`; keyboard decode exists
  (`scancode_decode` → `emit_key_event`, idt.cc:45); a scrollback grid
  + line editor + a few builtin commands is all that's missing. Serial
  console already proves the command surface pattern.
- **Hook points.** `kernel/console/fbcon.{h,cc}` (grid, scrollback,
  keyboard ring → line editor); `main.cc` boot flag (`con=fb|desk`)
  choosing fbcon vs deskman; builtins reuse `hxrt/apps/builtins.cc`
  command set.
- **Effort:** S/M. **Value:** debug shell without the compositor —
  and the TTY layer userspace will eventually print to.

### 6.5 Registry / structured config store

- **What it is.** Hierarchical key-value config persisted across
  boots — the Windows-registry/`sysctl`-by-name idea, backing
  settings, desktop state, service config.
- **Current base.** Pieces exist but are disjoint: `svc_data_set/get`
  (revsvc.h — 8 pairs per service, RAM-only), hypeblob gives durable
  files (`U:`, `S:/data`), and `kernel/db/hypedb.hpp` already wraps a
  freestanding **SQLite** port (`third_party/sqlite`) — SQL over a
  hypeblob file is a heavier-duty option. Recommended shape:
  `kernel/cfg/registry.{h,cc}` — a compact TLV tree serialized to
  `U:/system/registry.hcfg` via fs_write + hypeblob sync, dot-path get/
  set (`"desktop.theme.accent"`), notify hooks via the event model.
- **Ref:** Windows Registry design notes; FreeBSD `sysctl`/Linux
  `sysctl` + sysfs; SQLite as the heavyweight alternative already
  linked in.
- **Effort:** S/M. **Value:** ends per-feature ad-hoc persistence;
  every settings dialog (hxsettings.hxer exists) gets a backend.

---

## 7. Summary table

| # | Subsystem | Spec / source | Hook point (actual files) | Effort | Value |
|---|-----------|---------------|---------------------------|--------|-------|
| 1.1 | Preemptive SMP scheduler | Linux 2.6 runqueue, osdev multitasking, SDM Vol 3A §8 | `kernel/sched/scheduler.cc`+`context.cc`, `kernel/lmhtask.h`, `idt.cc:timer_handler`, `smp.cc:ap_park_loop`, `wait_queue.h` | L | Foundational |
| 1.2 | Kernel threads | Linux `kthread` | `kernel/sched/kthread.cc`; un-pump `main.cc:1080–1094` | M | Unblocks drivers |
| 1.3 | IRQ-driven net RX | virtio 1.2 §5.1.6 | `kernel/net/virtio_net.cc`, `idt.cc:idt_vector_register` | S/M | Latency + IRQ→thread template |
| 1.4 | Userspace: ring-3+TSS+GDT, addr spaces, syscall ABI, ELF64 | SDM §6–7, SysV ELF gABI, Linux `binfmt_elf.c` | new `arch/x86/gdt/tss/syscall_entry`, `memory/vmm.cc`, `syscall.cc`, `hxrt/elf.cc`, `hxpack.cc` | XL | "Universal kernel" |
| 1.5 | Per-task exception recovery | Linux `do_page_fault`/`die` | `idt.cc` exception branch, `sched::Task`, TSS IST | M/L | Reliability |
| 2 | SMP locking audit | osdev Spinlocks, Linux locking docs | `spinlock/mutex/wait_queue.h` → `pmm/heap/vfs/net/pullmix/winman/revsvc/guard/klog` | M | Correctness gate |
| 3.1 | Demand paging + swap + OOM | SDM §4, Linux `mm/` | `memory/vmm.cc` `#PF` hook in `idt.cc`, `memory/swap.cc`, `block/blockdev.h`, `memory/oom.cc` | XL | Soft memory limit |
| 4.1 | TCP | RFC 9293, lwIP model | `kernel/net/tcp.cc`, `net.cc` checksum/ARP, `fdtable.cc` socket fds | L | Real networking |
| 4.2 | DNS resolver | RFC 1035 | `kernel/net/dns.cc`, DHCP opt 6 in `net.cc` | S | Enables 4.3/4.4 |
| 4.3 | NTP sync | RFC 5905 (SNTP) | `kernel/net/ntp.cc`, `time/timer.cc` offset | S | TLS + log timestamps |
| 4.4 | TLS 1.3 + X.509 | RFC 8446/8439/7748/5280, BearSSL | `hcpp/chacha20/poly1305/x25519`, `net/tls.cc`, `net/x509.cc`, certs on `S:` via hypeblob | L | HTTPS/update channel |
| 4.5 | ping + netstat pane | RFC 792 | `net.cc` `icmp_echo_send`, `hxrt/apps/hxping.cc`, `revdbg.cc` | S | Debuggability |
| 5.1 | .hxer bytecode VM | Lua VM design, WASM MVP | `hxrt/hxbvm.cc`, `hxer.h` kind, `hxpack.cc`, `tools/` assembler | L | Real app ecosystem |
| 5.2 | Kernel modules | Linux module loader, kldload | `hxrt/modload.cc`, `fdtable` read, `revsvc` register, `ecdsa` signing | L | Post-boot drivers |
| 5.3 | IPC pipes + shm | POSIX `pipe(2)`/`shm_open`, BeOS ports | `kernel/ipc/pipe.cc`+`shm.cc`, `fdtable` pipe fds, `wait_queue.h`, `guard` `GuardOp::Ipc` | M | Apps become a system |
| 5.4 | epoll-like events | `epoll(7)`, `kqueue(2)` | `kernel/event/evq.cc`, `xau_pump` timers, fd readiness | M | Non-busy-wait API |
| 5.5 | POSIX libc subset | POSIX.1, musl (reference) | `userland/libhype/` on 1.4c syscall ABI; hcpp stays kernel-internal | L | Portability on-ramp |
| 6.1 | Crash dumps | Linux kdump/minidump | `kernel/log/kring.cc`, `panic.cc` → `U:/crash.log` or raw-LBA via `ata.cc` | S | Post-mortem debugging |
| 6.2 | Watchdog + hung tasks | Linux `watchdog`/`hung_task`, ICH TCO | `sched/watchdog.cc`, `smp.cc` counters, `idt.cc` tick | S/M | Field robustness |
| 6.3 | DTrace-like probes | DTrace SDT/FBT, Linux tracepoints | `kernel/dbg/probe.cc`, `KPROBE` sites, `guard/trace.h` ring model | S/L | Introspection |
| 6.4 | FB console mode | Linux VT, FreeBSD `vt(4)` | `kernel/console/fbcon.cc`, `draw_text`, scancode path, `main.cc` `con=` flag | S/M | Rescue/debug shell |
| 6.5 | Registry/config store | Windows Registry, `sysctl`; SQLite in `kernel/db/` as alt | `kernel/cfg/registry.cc` → `U:/system/registry.hcfg` via `fs_write`+hypeblob | S/M | Durable settings |

---

## 8. Recommended top 10 (ordered)

Ordering = dependency-respecting value-per-effort. The first three are
deliberately cheap wins that also build the machinery the big items
need.

1. **6.1 Crash dumps (S).** klog ring + panic → `U:/crash.log`/raw LBA.
   Every subsequent feature's failures become diagnosable. Zero deps.
2. **4.2 DNS resolver (S).** Unlocks NTP, TLS testing, any host-named
   service. Zero deps beyond the UDP stack we have.
3. **4.5 ping + netstat (S).** The net observability the next two big
   items are debugged with.
4. **1.1 Preemptive SMP scheduler (L).** The foundational gap; APs are
   already online and waiting. Fold the §2 locking audit (M) into the
   same project — they must ship together.
5. **1.2 Kernel threads (M).** Convert the idle-loop pumps
   (`net_poll`, `hasio`, `runnervx`, `fw`, `fs_sync`) into real
   threads; pair with 1.3 IRQ RX.
6. **4.1 TCP (L).** On the event-driven netd thread; sockets ride the
   fd table. Highest-visibility feature.
7. **6.4 FB console (S/M).** Debug shell without the desktop — and the
   future TTY substrate for 1.4.
8. **5.1 Bytecode VM .hxer (L).** App ecosystem without needing full
   userspace first; HypeGuard hooks become the VM syscall gate.
9. **6.2 Watchdog + hung-task detector (S/M).** Once threads exist,
   detect and report (and later kill) wedges.
10. **1.4 Userspace (XL, phased).** GDT/TSS + ring-3 → address spaces →
    syscall ABI → ELF .hxer loader. The end-state ask; everything above
    de-risks it. **Honorable mention:** 5.3 IPC (M) slots in at #6–7 if
    app-composability beats net features in priority; 4.4 TLS rides
    right behind TCP.

**Not recommended soon:** 3.1 swap/OOM (needs 1.4 address spaces to be
meaningful — revisit after), 5.2 modules (do the bytecode VM first —
same ecosystem payoff, no relocation complexity), 6.3 dynamic probes
(static `KPROBE` sites can land inside #4/#6 cheaply; INT3 patching
waits for SMP-safe code patching infrastructure).
