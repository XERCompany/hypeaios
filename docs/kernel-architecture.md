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

## Language direction

Assembly remains necessary for boot, interrupt entry, context switching and syscall entry. Larger kernel components should migrate to Rust `no_std` once the low-level ABI and build flow are stable.
