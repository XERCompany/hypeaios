#pragma once

#include "hcpp/types.hpp"

namespace hype::kvm {

    // KVM — kernel virtualization module (VMX skeleton, Linux-KVM-
    // inspired API). Detects Intel VMX (CPUID.1:ECX.VMX), negotiates
    // IA32_FEATURE_CONTROL, allocates VMXON/VMCS regions from the PMM,
    // enters VMX root operation via VMXON, and lays down the minimal
    // VMCS host/guest field writes a real VMLAUNCH will build on.
    //
    // Companion submodules (all header-only under kernel/kvm/):
    //   kvm-guardmemfd.h — guest-physical memory slots + guard pages
    //   kvm-asyncpf.h    — per-vcpu async page-fault queue
    //   kvm-dirtyring.h  — PML-style dirty-page ring per VM
    //   kvm-mmiofd.h     — MMIO window routing (virtio-mmio plumbing)
    //
    // Boot markers over serial:
    //   "kvm vmx=ok" / "kvm vmx=absent" — hardware + MSR gate
    //   "kvm vmxon=ok" / "kvm vmxon=fail s=<step>" — VMX root entered
    //   "kvm modules=<n>" — companion submodules compiled in
    //
    // Under QEMU the VMX bit only appears with -cpu host or +vmx; on a
    // plain -cpu qemu64 guest the module logs vmx=absent and the stage
    // still succeeds — KVM is optional hardware.

    // Aggregate capability/state snapshot for diagnostics.
    struct VmxInfo {
        bool cpuid_vmx;              // CPUID.1:ECX.VMX (bit 5)
        bool under_hypervisor;       // CPUID.1:ECX bit 31 (VMX may be hidden)
        bool feature_control_locked; // IA32_FEATURE_CONTROL bit 0
        bool vmxon_outside_smx;      // IA32_FEATURE_CONTROL bit 2
        bool vmxon_ok;               // VMX root operation entered
        bool vmcs_loaded;            // VMCS allocated + vmptrld + field writes done
        hcpp::u32 vmcs_revision;     // IA32_VMX_BASIC[30:0] — VMCS revision id
        hcpp::u32 vmcs_memory_type;  // IA32_VMX_BASIC[53:50] — required memtype
        hcpp::uintptr_t vmxon_region;// physical address of the VMXON page
        hcpp::uintptr_t vmcs_region; // physical address of the VMCS page
    };

    // CPUID + IA32_FEATURE_CONTROL gate: can this CPU enter VMX root?
    bool vmx_supported() noexcept;

    // Locks IA32_FEATURE_CONTROL if needed, sets CR4.VMXE, allocates the
    // VMXON page, writes the VMCS revision id, executes VMXON, then
    // allocates a VMCS page, vmclear + vmptrld + skeleton field writes.
    bool vmx_enable() noexcept;

    bool vmx_enabled() noexcept;
    const VmxInfo &vmx_info() noexcept;

    // Number of companion submodules compiled into kernel/kvm/
    // (guardmemfd, asyncpf, dirtyring, mmiofd). Reported in the
    // "kvm modules=N" boot marker.
    hcpp::u32 kvm_module_count() noexcept;

    // Runtime-stage entry point (stage "kvm.vmx"). Prints the serial
    // markers; always returns — a CPU without VMX is not a boot failure.
    bool kvm_initialize() noexcept;

    // Embedded guest initramfs (payload/guest/initramfs.cpio via
    // initramfsdata.S). size_out receives the blob length in bytes;
    // the returned pointer is never null (placeholder blob counts).
    const void *kvm_initramfs(hcpp::size_t &size_out) noexcept;

    // Guest-side hypercall stub: executes VMCALL with `number` in RAX.
    // Only meaningful inside a VMX *non-root* guest (or as the future
    // guest-kernel ABI); executing it in VMX root faults with #UD, so
    // callers must gate on running inside a guest. Kept for the
    // vmxon/vmmcall minimal flow.
    hcpp::u64 kvm_vmmcall(hcpp::u64 number, hcpp::u64 arg0, hcpp::u64 arg1) noexcept;

}  // namespace hype::kvm
