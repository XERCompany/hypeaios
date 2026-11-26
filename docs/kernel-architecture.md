# HypeAI OS kernel architecture

HypeCore is planned as a small capability-oriented microkernel. Hardware drivers and higher-level services should run outside the kernel whenever practical.

## Layers

1. **HypeBoot**: firmware-facing bootloader and boot protocol.
2. **HypeCore**: memory management, interrupts, threads, scheduling, syscalls, IPC and capabilities.
3. **HypeHAL**: architecture and platform abstraction for x86_64 hardware.
4. **HypeServices**: storage, VFS, networking, input, display and AI runtime services.
5. **HypeAPI**: stable syscall ABI and IPC contracts for userspace programs.

## Milestone state

M1 proved the custom boot chain reaches x86_64 long mode. The current prototype adds a freestanding HypeAICode++ kernel runtime, VBE framebuffer handoff, EDID monitor reporting, staged runtime startup, PCI scanning, ACPI discovery, E820-based physical memory allocation and active PIC/PIT/keyboard interrupt plumbing.

## Next implementation order

1. Replace fixed kernel placement with a relocatable format.
2. Parse MADT and move from legacy PIC to APIC/x2APIC.
3. Add kernel virtual memory and physical-page ownership metadata.
4. Implement context switching and ring 3 userspace.
5. Add HPET timekeeping and a keyboard scancode decoder.
6. Implement PCI storage drivers and the first boot filesystem.


## Linux-inspired OS Core module pass

The custom `kernel/*.h` filenames are not public Linux or POSIX API names. Internet/source inspection therefore maps them by intended subsystem rather than by exact upstream filename. The current implementation adapts Linux design concepts without copying GPL implementation code:

- input: Linux-style typed input events, key bitsets, input device registration and sync events;
- VFS/filesystem: Linux-style `FileOperations`, open-file state, seek/read/write dispatch and a small memory-backed quick filesystem;
- security/users: Linux-inspired credentials, UID/GID and capability bits;
- LMH tasks/processes: Linux-inspired process/task lifecycle, priorities and a fixed-capacity scheduler queue;
- IMMT memory: fixed-block kernel allocator with allocation statistics;
- IMMT core: module registration, priority-ordered initialization and reverse shutdown;
- host devices: Linux-inspired device/driver registration, matching, probe and remove callbacks.

Linux references inspected: `include/linux/fs.h`, `include/linux/input.h`, `include/linux/cred.h`, `include/linux/sched.h` and `include/linux/device.h` in the upstream `torvalds/linux` tree.

## Runtime, PCI and cross-platform kernel primitives

`kernel/runtime` now owns staged OS Core startup. Runtime stages initialize core services and scan PCI before framebuffer/monitor reporting.

PCI support is split into configuration-space access, I/O/BAR helpers, capability-list parsing and MSI/MSI-X control. Chip identification is split by vendor family (`intel`, `qemu`, `virtio`) plus generic PCI class mapping.

Linux/Windows-inspired primitives now include atomic/interlocked operations, spinlocks, mutexes, semaphores, wait queues, IRQ dispatch registration, syscall dispatch, thread priority metadata, kernel object handles and NTSTATUS-style error codes. They are architecture-level building blocks, not ABI-compatible clones of Linux or Windows.

## Memory, ACPI and interrupt foundation

The BIOS bootloader now captures the E820 memory map before leaving real mode. `kernel/memory/pmm` uses usable E820 regions to manage the first 128 MiB of physical memory through a bitmap allocator, reserves the first MiB for boot/kernel/BIOS data, supports contiguous page allocation and exposes allocation statistics. The runtime performs an allocate/free self-test during boot.

`kernel/acpi` searches the RSDP in the EBDA and BIOS ROM area, validates checksums, parses either RSDT or XSDT entries, and records MADT/FADT table locations for later APIC and power-management drivers.

`kernel/interrupts` installs a full 256-entry x86_64 IDT with assembly interrupt stubs, remaps the legacy PIC, initializes the PIT at 100 Hz, bridges keyboard IRQ1 scancodes into the input-event subsystem and enables IRQ0/IRQ1. QEMU validation confirms timer interrupts advance while the kernel is in long mode.

## Language direction

Assembly remains necessary for boot, interrupt entry, context switching and syscall entry. Larger kernel components should migrate to Rust `no_std` once the low-level ABI and build flow are stable.
