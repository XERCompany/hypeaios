#include "kernel/kvm/kvm.h"

#include "hcpp/memory.hpp"
#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/kvm/kvm-asyncpf.h"
#include "kernel/kvm/kvm-dirtyring.h"
#include "kernel/kvm/kvm-guardmemfd.h"
#include "kernel/kvm/kvm-mmiofd.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

// VMX bring-up skeleton, Linux-KVM-flavoured. Design notes:
//
//  * Detection: CPUID.1:ECX.VMX (bit 5) says the silicon has VT-x;
//    IA32_FEATURE_CONTROL (MSR 0x3A) says the BIOS let us have it.
//    If the MSR is unlocked we set lock + VMXON-outside-SMX ourselves;
//    if it is locked with VMXON disabled there is no software fix.
//  * VMXON region: one PMM page. Its first dword must carry the VMCS
//    revision id from IA32_VMX_BASIC[30:0] with bit 31 clear (a set
//    bit 31 marks a "shadow" VMCS which we do not use). PMM frames are
//    all below 128 MiB so the IA32_VMX_BASIC bit-55 width restriction
//    (regions above 4 GiB forbidden when the bit reads 0) can never
//    bite us.
//  * Sequence: CR4.VMXE=1 -> VMXON [pa] -> VMCLEAR [vmcs] ->
//    VMPTRLD [vmcs] -> a handful of VMWRITEs for the host-state area
//    and guest-state skeleton -> VMCLEAR (leaves no current VMCS
//    dangling). All VMX instructions report failure on CF (VMfail-
//    Invalid) or ZF (VMfailValid); "seta" catches success (CF=0,ZF=0).
//  * TODO for a real VMLAUNCH: host segment bases/TR/IDT/GDT fields,
//    guest segment + descriptor-table fields, EPT pointer + EPTP,
//    pin/proc/exit/entry control MSRs (VMX_TRUE_* variants), MSR/I/O
//    bitmaps, exit stack, and the vmexit dispatch loop. The field
//    writes present here are real VMCS encodings, not placeholders —
//    they are simply not yet a complete launchable state.

extern "C" hcpp::u8 initramfs_data[];
extern "C" hcpp::u8 initramfs_end[];

namespace hype::kvm {
    namespace {
        // MSRs.
        constexpr hcpp::u32 msr_ia32_feature_control = 0x3A;
        constexpr hcpp::u32 msr_ia32_vmx_basic = 0x480;

        constexpr hcpp::u64 feature_control_lock = 1ull << 0;
        constexpr hcpp::u64 feature_control_vmxon_smx = 1ull << 1;
        constexpr hcpp::u64 feature_control_vmxon_outside_smx = 1ull << 2;

        // CPUID.1:ECX bits.
        constexpr hcpp::u32 cpuid_ecx_vmx = 1u << 5;
        constexpr hcpp::u32 cpuid_ecx_hypervisor = 1u << 31;

        // CR4.VMXE.
        constexpr hcpp::uintptr_t cr4_vmxe = 1ull << 13;

        // VMCS field encodings actually written below (Intel SDM vol. 3C
        // appendix B — natural-width host/guest state fields).
        constexpr hcpp::u64 vmcs_host_cr0 = 0x6C00;
        constexpr hcpp::u64 vmcs_host_cr3 = 0x6C02;
        constexpr hcpp::u64 vmcs_host_cr4 = 0x6C04;
        constexpr hcpp::u64 vmcs_host_rsp = 0x6C14;
        constexpr hcpp::u64 vmcs_host_rip = 0x6C16;
        constexpr hcpp::u64 vmcs_guest_cr0 = 0x6800;
        constexpr hcpp::u64 vmcs_guest_cr3 = 0x6802;
        constexpr hcpp::u64 vmcs_guest_cr4 = 0x6804;
        constexpr hcpp::u64 vmcs_guest_rflags = 0x6820;
        constexpr hcpp::u64 vmcs_guest_rip = 0x681E;
        constexpr hcpp::u64 vmcs_guest_rsp = 0x681C;

        VmxInfo info{};
        bool enabled = false;
        hcpp::u8 fail_step = 0;  // where vmx_enable() stopped, for "s=<n>"

        hcpp::u64 read_msr(hcpp::u32 msr) noexcept {
            hcpp::u32 low, high;
            asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
            return (static_cast<hcpp::u64>(high) << 32) | low;
        }

        void write_msr(hcpp::u32 msr, hcpp::u64 value) noexcept {
            asm volatile("wrmsr" ::"a"(static_cast<hcpp::u32>(value)),
                         "d"(static_cast<hcpp::u32>(value >> 32)), "c"(msr));
        }

        // VMX instructions: all take/report via CF/ZF. `seta` = CF=0 and
        // ZF=0 = success. m64 operands must be 8-byte-aligned slots
        // holding the physical address.
        bool vmxon(hcpp::u64 region_pa) noexcept {
            hcpp::u8 ok = 0;
            asm volatile("vmxon %[pa]; seta %[ok]"
                         : [ok] "=q"(ok)
                         : [pa] "m"(region_pa)
                         : "cc", "memory");
            return ok != 0;
        }

        // VMXOFF is the other half of the VMXON flow; not called yet
        // because the module stays in root mode once enabled (VMX
        // teardown for reboot/kexec is a TODO).
        [[maybe_unused]] bool vmxoff() noexcept {
            hcpp::u8 ok = 0;
            asm volatile("vmxoff; seta %[ok]" : [ok] "=q"(ok)::"cc");
            return ok != 0;
        }

        bool vmclear(hcpp::u64 vmcs_pa) noexcept {
            hcpp::u8 ok = 0;
            asm volatile("vmclear %[pa]; seta %[ok]"
                         : [ok] "=q"(ok)
                         : [pa] "m"(vmcs_pa)
                         : "cc", "memory");
            return ok != 0;
        }

        bool vmptrld(hcpp::u64 vmcs_pa) noexcept {
            hcpp::u8 ok = 0;
            asm volatile("vmptrld %[pa]; seta %[ok]"
                         : [ok] "=q"(ok)
                         : [pa] "m"(vmcs_pa)
                         : "cc", "memory");
            return ok != 0;
        }

        bool vmwrite(hcpp::u64 field, hcpp::u64 value) noexcept {
            hcpp::u8 ok = 0;
            asm volatile("vmwrite %[val], %[field]; seta %[ok]"
                         : [ok] "=q"(ok)
                         : [val] "r"(value), [field] "r"(field)
                         : "cc");
            return ok != 0;
        }

        // Future vmexit landing pad: HOST_RIP points here. A real exit
        // path would save guest GPRs, read VM-exit reason/qualification
        // and dispatch (EPT violation -> mmiofd_route_*, PML full ->
        // dirtyring_mark batch, etc.). Today nothing can reach it — no
        // VMLAUNCH exists yet — but the VMCS write must name a real
        // host RIP to stay honest.
        void vmexit_entry() noexcept {
            serial::write_line("kvm vmexit=unexpected");
            for (;;) hype_cpu_halt();
        }

        bool load_vmcs() noexcept {
            const hcpp::uintptr_t vmcs_pa = memory::pmm_allocate_page();
            if (vmcs_pa == 0) {
                fail_step = 4;
                return false;
            }
            info.vmcs_region = vmcs_pa;
            hcpp::set(reinterpret_cast<void *>(vmcs_pa), 0, memory::page_size);
            *reinterpret_cast<volatile hcpp::u32 *>(vmcs_pa) = info.vmcs_revision;

            if (!vmclear(vmcs_pa)) {
                fail_step = 5;
                return false;
            }
            if (!vmptrld(vmcs_pa)) {
                fail_step = 6;
                return false;
            }

            // Host-state area: snapshot the CPU state we would want back
            // after a VM exit.
            const hcpp::uintptr_t host_cr0 = hype_cpu_read_cr0();
            const hcpp::uintptr_t host_cr3 = hype_cpu_read_cr3();
            const hcpp::uintptr_t host_cr4 = hype_cpu_read_cr4() | cr4_vmxe;
            hcpp::uintptr_t host_rsp = 0;
            asm volatile("mov %%rsp, %0" : "=r"(host_rsp));
            if (!vmwrite(vmcs_host_cr0, host_cr0) ||
                !vmwrite(vmcs_host_cr3, host_cr3) ||
                !vmwrite(vmcs_host_cr4, host_cr4) ||
                !vmwrite(vmcs_host_rsp, host_rsp) ||
                !vmwrite(vmcs_host_rip,
                         reinterpret_cast<hcpp::u64>(&vmexit_entry))) {
                fail_step = 7;
                return false;
            }
            // TODO(host): HOST_CS/SS/DS/ES/FS/GS/TR selectors +
            // FS/GS/TR bases, GDTR/IDTR bases+limits, IA32_SYSENTER_*,
            // IA32_EFER, exit-stack top (host RSP above is the *current*
            // stack, which is wrong for exits once scheduling exists).

            // Guest-state skeleton: mirror the host control regs so a
            // first VMLAUNCH only needs entry point + segments filled.
            if (!vmwrite(vmcs_guest_cr0, host_cr0) ||
                !vmwrite(vmcs_guest_cr3, host_cr3) ||
                !vmwrite(vmcs_guest_cr4, hype_cpu_read_cr4() & ~cr4_vmxe) ||
                !vmwrite(vmcs_guest_rflags, 0x2) ||
                !vmwrite(vmcs_guest_rip, 0) ||
                !vmwrite(vmcs_guest_rsp, 0)) {
                fail_step = 8;
                return false;
            }
            // TODO(guest): real entry RIP/RSP from the guest boot flow,
            // all segment selectors/bases/limits/access-rights, GDTR/
            // IDTR, IA32_EFER, plus the controls: pin-based, primary/
            // secondary proc-based, exit/entry controls, exception
            // bitmap, CR0/CR4 masks+shadows, EPTP + guest->host map,
            // MSR/I-O bitmaps, and the async-PF vector plumbing.

            // Drop the current-VMCS binding; the region stays valid for
            // a later vmptrld when launch support lands.
            if (!vmclear(vmcs_pa)) {
                fail_step = 9;
                return false;
            }
            info.vmcs_loaded = true;
            return true;
        }
    }  // namespace

    bool vmx_supported() noexcept {
        const x86::CpuFeatures &cpu = x86::cpuid_features();
        info.cpuid_vmx = (cpu.leaf1_ecx & cpuid_ecx_vmx) != 0;
        info.under_hypervisor = (cpu.leaf1_ecx & cpuid_ecx_hypervisor) != 0;
        if (!info.cpuid_vmx) return false;

        const hcpp::u64 fc = read_msr(msr_ia32_feature_control);
        info.feature_control_locked = (fc & feature_control_lock) != 0;
        info.vmxon_outside_smx = (fc & feature_control_vmxon_outside_smx) != 0;
        // Locked MSR with VMXON-outside-SMX clear = BIOS disabled VT-x.
        if (info.feature_control_locked && !info.vmxon_outside_smx) {
            return false;
        }
        return true;
    }

    bool vmx_enable() noexcept {
        fail_step = 0;
        if (!vmx_supported()) {
            fail_step = 1;
            return false;
        }

        // Lock IA32_FEATURE_CONTROL if the BIOS left it open, enabling
        // VMXON both inside and outside SMX operation.
        hcpp::u64 fc = read_msr(msr_ia32_feature_control);
        if ((fc & feature_control_lock) == 0) {
            fc |= feature_control_lock | feature_control_vmxon_smx |
                  feature_control_vmxon_outside_smx;
            write_msr(msr_ia32_feature_control, fc);
            info.feature_control_locked = true;
            info.vmxon_outside_smx = true;
        }

        // VMCS revision id + required memory type for VMX regions.
        const hcpp::u64 basic = read_msr(msr_ia32_vmx_basic);
        info.vmcs_revision = static_cast<hcpp::u32>(basic & 0x7FFFFFFF);
        info.vmcs_memory_type =
            static_cast<hcpp::u32>((basic >> 50) & 0xF);

        const hcpp::uintptr_t vmxon_pa = memory::pmm_allocate_page();
        if (vmxon_pa == 0) {
            fail_step = 2;
            return false;
        }
        info.vmxon_region = vmxon_pa;
        hcpp::set(reinterpret_cast<void *>(vmxon_pa), 0, memory::page_size);
        *reinterpret_cast<volatile hcpp::u32 *>(vmxon_pa) = info.vmcs_revision;

        hype_cpu_write_cr4(hype_cpu_read_cr4() | cr4_vmxe);
        if (!vmxon(vmxon_pa)) {
            fail_step = 3;
            hype_cpu_write_cr4(hype_cpu_read_cr4() & ~cr4_vmxe);
            return false;
        }
        info.vmxon_ok = true;

        const bool vmcs_ok = load_vmcs();
        enabled = vmcs_ok;
        return vmcs_ok;
    }

    bool vmx_enabled() noexcept { return enabled; }
    const VmxInfo &vmx_info() noexcept { return info; }

    hcpp::u32 kvm_module_count() noexcept {
        // Companion submodule headers compiled into this TU. Bump when
        // kernel/kvm/ gains another module.
        return 4;  // guardmemfd + asyncpf + dirtyring + mmiofd
    }

    const void *kvm_initramfs(hcpp::size_t &size_out) noexcept {
        size_out = static_cast<hcpp::size_t>(initramfs_end - initramfs_data);
        return initramfs_data;
    }

    hcpp::u64 kvm_vmmcall(hcpp::u64 number, hcpp::u64 arg0,
                          hcpp::u64 arg1) noexcept {
        // Guest ABI stub: VMCALL traps to the hypervisor when executed
        // in VMX non-root. In VMX root it faults (#UD) — callers must be
        // guest code. Kept as the call-side half of the vmxon/vmmcall
        // minimal flow.
        hcpp::u64 out = 0;
        asm volatile("vmcall"
                     : "=a"(out)
                     : "a"(number), "D"(arg0), "S"(arg1)
                     : "cc", "memory");
        return out;
    }

    bool kvm_initialize() noexcept {
        if (!vmx_supported()) {
            serial::write_line("kvm vmx=absent");
            return true;
        }
        serial::write_line("kvm vmx=ok");
        if (!vmx_enable()) {
            serial::write("kvm vmxon=fail s=");
            serial::write_dec32(fail_step);
            serial::write("\r\n");
        } else {
            serial::write_line("kvm vmxon=ok");
        }
        serial::write("kvm modules=");
        serial::write_dec32(kvm_module_count());
        serial::write("\r\n");
        return true;
    }

}  // namespace hype::kvm
