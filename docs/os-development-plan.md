# HypeAI OS full development plan

Target: a from-scratch x86_64 operating system using a custom legacy-BIOS boot chain. Linux and Windows may inspire subsystem design, but neither is part of the target OS.

## Stage 1 — boot and platform foundation
Completed: BIOS boot sectors, protected/long mode transition, VBE framebuffer, EDID monitor information, HypeAICode++ freestanding runtime, E820 memory map, physical memory manager, ACPI discovery, IDT/PIC/PIT/keyboard interrupts, x86_64 assembly primitives, cooperative context switching and kernel-thread validation.

Next:
1. Replace fixed kernel placement with a structured kernel image format.
2. Parse CPU topology and CPU features in detail.
3. Move interrupt routing from legacy PIC to IOAPIC/local APIC using MADT.
4. Add HPET timekeeping and calibrate timers.

## Stage 2 — kernel memory architecture
1. Ownership metadata for every physical frame.
2. Kernel virtual memory regions and permission propagation.
3. Kernel heap backed by physical pages.
4. Page fault handler and demand paging diagnostics.
5. Separate kernel/user page-table preparation.
6. Memory-mapped device registry.

## Stage 3 — execution architecture
1. Kernel thread objects integrated with LMH task/process metadata.
2. Preemptive scheduling from timer interrupts with full register-state switching.
3. Thread priorities, sleep queues and wait queues connected to real blocking.
4. syscall/sysenter or syscall instruction entry in assembly.
5. ELF loader and first ring3 userspace process.
6. `init` process and process lifecycle syscalls.

## Stage 4 — input, console and shell
1. Complete keymap layers, modifiers and keyboard layouts.
2. Input event ring buffers and device-independent key events.
3. Serial/framebuffer console abstraction.
4. Kernel shell with diagnostic commands.
5. Userspace shell after ring3 is available.

## Stage 5 — storage and filesystems
1. PCI mass-storage controller detection.
2. ATA PIO or AHCI driver.
3. Partition table parser.
4. Block cache.
5. Boot filesystem driver connected to quickfs/VFS.
6. Executable/module loading from disk.

## Stage 6 — display architecture
1. Dynamic VBE mode enumeration while still in BIOS boot.
2. PCI GPU device abstraction.
3. Native GPU framebuffer modesetting.
4. GPU I2C/DDC channel and monitor control service.
5. HDMI/DisplayPort HDR metadata and 10-bit scanout.
6. Compositor and window service.

## Stage 7 — devices and services
1. APIC/x2APIC SMP startup.
2. USB xHCI and HID drivers.
3. Network device drivers and network stack.
4. Device/driver service isolation.
5. Persistent configuration service.
6. Userspace system services.

## Stage 8 — HypeAI runtime layer
1. HypeAI runtime userspace service above the kernel.
2. IPC API for AI agents.
3. Sandboxed compute jobs and capability permissions.
4. Model storage through filesystem service.
5. Hardware acceleration abstraction.

## Continuous rules
- Headers contain declarations; implementations stay in `.cc`.
- Assembly is isolated under `kernel/arch/x86` for CPU, I/O, interrupt entry and context switching.
- Every runtime stage must have a deterministic QEMU self-test when practical.
- No Linux code is copied; subsystem designs are adapted where suitable.
