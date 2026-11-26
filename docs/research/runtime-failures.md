# Runtime stage failures — root cause analysis (branch `devin/hypeai-font-ui`)

Investigated 2026-11-26.

## TL;DR — ONE root cause explains every failure

**The seven `RuntimeStage` globals in `kernel/runtime/runtime.cc` require dynamic (runtime) C++ initialization that never executes.** `RuntimeStage` contains an `hcpp::StringView` whose constructor `StringView(const char*)` is **not `constexpr`** — it calls `length(text)` at runtime (`hcpp/string_view.cc:7`, declared `hcpp/string_view.hpp:10`). Therefore the aggregate initializers at `runtime.cc:86–92` are not constant expressions, the compiler emits `.init_array` initializers, and this freestanding kernel **never invokes static initializers**: `kernel/entry.asm:7-10` jumps straight into `hype_kernel_main`, `kernel/linker.ld` does not collect `.init_array`, and `ld -nostdlib` provides no crt0 (`Makefile`).

The stage objects therefore sit zeroed in `.bss`: `enter == nullptr` → `register_stage` rejects all seven (`runtime.cc:110`) → `stage_count == 0` → `run_stages()` is a trivial no-op returning `stages_failed == 0` = **true** (`runtime.cc:116–135`) → every `RuntimeInfo` field stays 0 → every marker prints failed/absent.

### Why this is provable (not just a guess)

- `interrupts=failed` is **impossible** if `interrupt_enter` ran: `idt_initialize()` sets `initialized = true` unconditionally (`interrupts/idt.cc:51`) and `interrupt_enter` then sets `current.interrupts_ready = interrupts_initialized()` (`runtime.cc:82-83`) — it can only return true.
- `pci devices=0` is impossible on QEMU `-machine pc` if `pci::scan()` ran (`pci/pci.cc:49-58`): the host bridge at bus 0 slot 0 always responds to config mechanism #1 (ports 0xCF8/0xCFC).
- `core services running=no` is impossible if `initialize_services()` completed: `immt::start()` (`immtcore.cc:50-55`) succeeds trivially with zero registered modules after `immt::initialize()`.
- `video devices=0`: `v4lv_device_count()` (`videov4lv.cc:13`) only grows via `v4lv_register_device` ← `virtual_video_register()` called inside `context_enter` (`runtime.cc:70`) — never ran.
- `madt=no cpus=0 ioapics=0 apic=unavailable`, `acpi=no tables=0`: all set inside `acpi_enter` (`runtime.cc:52-64`) — never ran. (`apic_supported` needs CPUID.1:EDX.9 which is always set on x86-64 QEMU.)
- `e820=0 usable=0 memory free=0MB`, `vmm=failed`, `context switch=failed`, `kernel threads=failed`: `RuntimeInfo` fields never populated.
- Boot itself works because nothing on that path needs dynamic init (`serial::initialize`, `fonts::initialize_default_fonts`, and the direct serial prints in `main.cc` run fine).

## Bootloader audit — e820 fields ARE populated correctly

- `boot/stage2.asm:106` writes `e820_physical` (`dword` at `BOOT_INFO_ADDR+41` = `0x5500`), `init_e820` (`stage2.asm:114-142`) increments `e820_entries` at `+45`, `boot_drive` at `+47`, kernel at `+48/+52`.
- `kernel/boot_info.hpp:31-35` places `e820_physical` at offset 41, `e820_entries` at 45, `boot_drive` at 47 — **consistent** (verified against `docs/boot-protocol.md:49-53`; `static_assert(sizeof==64)` holds).
- `init_e820` itself is a correct int 15h/E820 loop (ES:DI=0:0x5500, EBX continuation, SMAP check, ≤32 entries). **e820=0 is a consequence of the stage not running, not a bootloader bug.** Under SeaBIOS it will produce entries once stages run.

## Proposed fixes

**Primary fix (pick one, do both ideally):**
- (a) Make `hcpp::StringView` fully `constexpr`: mark all three constructors and `length()` `constexpr` (`string_view.hpp:9-11,19`; `string_view.cc:5-7,19`) — a `while` loop is permitted in constexpr since C++14. This makes `RuntimeStage` globals constant-initialized → baked into `.rodata` → `register_stage` works.
- (b) Alternatively change `RuntimeStage::name` to `const char*` (`runtime.h:13`) — also removes dynamic init.

**Defense in depth:** export `.init_array` bounds in `linker.ld` (`__init_array_start`/`__init_array_end`) and invoke them at the top of `hype_kernel_main` (or `kernel_entry`), so any future dynamically-initialized global works. Also audit for other globals: `kernel/dialog_dev.cc:5` (`StringView title`, benign — zero-init equivalent) is the only other candidate.

**Secondary fix:** `run_stages()` (`runtime.cc:116-135`) returns success when zero stages are registered. Add a `stage_count == 0 → fail` guard and/or print `stages_completed/stages_failed` to serial so this failure class is diagnosable.

**Minor:** `pmm_initialize` only reserves 0–1MB (`pmm.cc:52`). Kernel (0x10000+`kernel_size`), page tables (0x70000-0x75FFF), boot_info (0x5000), e820 (0x5500), EDID (0x5200) are all under 1MB today, but nothing reserves based on `boot_info.kernel_size` — a kernel > ~960KB would be handed out as free frames.

## Post-fix expectations (verified code is otherwise sound)

- `pmm.cc` logic is correct given a populated e820 map; `vmm_selftest` depends on PMM only.
- `acpi.cc:57-64` scans EBDA (`0x40E`) + `0xE0000-0xFFFFF` — finds SeaBIOS RSDP.
- `idt_stubs.asm` has a complete 256-entry `isr_stub_table`; `isr_common` stack offsets are correct.
- `context.asm` (6 pops + `ret`) matches `prepare_stack` layout (`context.cc:16-25`); scheduler selftest is sound.
- `kernel/thread.cc` exists but is unused by the runtime markers — "kernel threads" prints `scheduler_ready` (`main.cc:252-253`).
