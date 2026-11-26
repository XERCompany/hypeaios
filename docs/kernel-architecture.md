# HypeAI OS kernel architecture

HypeCore is planned as a small capability-oriented microkernel. Hardware drivers and higher-level services should run outside the kernel whenever practical.

## Layers

1. **HypeBoot**: firmware-facing bootloader and boot protocol.
2. **HypeCore**: memory management, interrupts, threads, scheduling, syscalls, IPC and capabilities.
3. **HypeHAL**: architecture and platform abstraction for x86_64 hardware.
4. **HypeServices**: storage, VFS, networking, input, display and AI runtime services.
5. **HypeAPI**: stable syscall ABI and IPC contracts for userspace programs.

## Milestone state

M1 proved the custom boot chain reaches x86_64 long mode. The current M2 prototype adds a freestanding HypeAICode++ kernel runtime, VBE linear framebuffer handoff, direct pixel/rectangle drawing and 5x7 framebuffer text output.

## Next implementation order

1. Capture E820 memory map in Stage 2.
2. Define a boot information structure passed to the kernel.
3. Add exception handlers and an IDT.
4. Implement physical memory allocation.
5. Replace fixed kernel placement with a relocatable format.
6. Add timer and keyboard interrupts.
7. Implement context switching and ring 3 userspace.


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

## Language direction

Assembly remains necessary for boot, interrupt entry, context switching and syscall entry. Larger kernel components should migrate to Rust `no_std` once the low-level ABI and build flow are stable.
