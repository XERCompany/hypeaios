#include "kernel/runtime/runtime.h"

#include "kernel/acpi/acpi.h"
#include "kernel/acpi/apic.h"
#include "kernel/acpi/madt.h"
#include "kernel/arch/x86/apic.h"
#include "kernel/arch/x86/smp.h"
#include "kernel/core_services.h"
#include "kernel/firewire/firewirebus.h"
#include "kernel/interrupts/idt.h"
#include "kernel/klog.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/pci/pci.h"
#include "kernel/sched/context.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/timer.h"
#include "kernel/usb/xhci.h"
#include "kernel/video/videoclient.h"
#include "kernel/video/videocoretest.h"
#include "kernel/video/videovirtual.h"

namespace hype::runtime {
    namespace {
        constexpr hcpp::size_t max_stages = 16;
        RuntimeStage* stages[max_stages];
        hcpp::size_t stage_count;
        RuntimeInfo current{};
        bool is_booted;
        const BootInfo* boot_info_pointer;

        bool core_enter(void*) noexcept {
            core::initialize_services();
            return core::services_running();
        }
        bool pci_enter(void*) noexcept {
            current.pci_devices = pci::scan();
            return true;
        }
        bool memory_enter(void*) noexcept {
            if (boot_info_pointer == nullptr) return false;
            memory::pmm_initialize(*boot_info_pointer);
            current.memory_initialized = memory::pmm_initialized();
            const hcpp::uintptr_t page = memory::pmm_allocate_page();
            current.memory_selftest = page != 0 && memory::pmm_free_page(page);
            const auto stats = memory::pmm_stats();
            current.memory_free_bytes = stats.free_bytes;
            current.memory_e820_entries = stats.e820_entries;
            current.memory_usable_regions = stats.usable_regions;
            return current.memory_initialized && current.memory_selftest;
        }
        bool vmm_enter(void*) noexcept {
            memory::vmm_initialize();
            current.vmm_initialized = memory::vmm_initialized();
            current.vmm_selftest = memory::vmm_selftest();
            return current.vmm_initialized && current.vmm_selftest;
        }
        bool acpi_enter(void*) noexcept {
            acpi::initialize();
            current.acpi_present = acpi::info().present;
            current.acpi_tables = acpi::info().table_count;
            acpi::madt_initialize();
            acpi::apic_initialize();
            current.madt_present = acpi::madt_info().present;
            current.apic_cpus = acpi::madt_info().enabled_cpu_count;
            current.ioapics = acpi::madt_info().ioapic_count;
            current.apic_supported = acpi::apic_info().cpuid_supported && acpi::apic_info().msr_enabled;
            current.apic_base = acpi::apic_info().apic_base;
            return true;
        }
        bool smp_enter(void*) noexcept {
            // Runs after interrupts.foundation (the LAPIC MMIO window
            // carries the INIT-SIPI-SIPI IPIs) and after time.timer
            // (uptime_ns backs the inter-IPI delays and the bounded
            // started-flag wait). Never fails the boot: on a 1-CPU
            // machine or with no live LAPIC it just prints
            // "smp cpus=1" and returns false internally.
            x86::smp_initialize();
            return true;
        }
        bool usb_enter(void*) noexcept {
            // Runs after interrupts.foundation (PIT ticks drive all the
            // bounded TRB waits) and after pci.scan + memory.pmm. USB is
            // optional hardware: usb_initialize() always returns so this
            // stage can never fail the boot.
            usb::usb_initialize();
            return true;
        }
        bool firewire_enter(void*) noexcept {
            // Runs after pci.scan + memory.pmm (DMA buffers) and
            // interrupts.foundation + time.timer (bounded waits). FW is
            // optional hardware: fw_initialize() prints dev=absent and
            // returns so this stage can never fail the boot.
            fw::fw_initialize();
            return true;
        }
        bool context_enter(void*) noexcept {
            current.context_switch_ready = sched::context_selftest();
            sched::scheduler_initialize();
            current.scheduler_ready = sched::scheduler_selftest();
            video::video_client_initialize();
            video::virtual_video_register();
            if (!video::video_core_selftest()) return false;
            return current.context_switch_ready && current.scheduler_ready;
        }
        bool interrupt_enter(void*) noexcept {
            interrupts::idt_initialize();
            interrupts::pic_initialize();
            interrupts::pit_initialize(100);
            interrupts::keyboard_initialize();
            interrupts::mouse_initialize();
            // irq_enable() drives the 8259 pair while the PIC path is
            // live. IRQ12 lives on the slave PIC; the cascade line
            // (master IRQ2) must be unmasked for slave IRQs to arrive.
            interrupts::irq_enable(0);
            interrupts::irq_enable(1);
            interrupts::irq_enable(2);
            interrupts::irq_enable(12);
            interrupts::interrupts_enable();
            // Try to hand the interrupt path to LAPIC + IOAPIC. On
            // success the 8259 pair is masked and the same irq_enable()
            // calls open the lines through IOAPIC redirection entries
            // instead; on failure the PIC path above keeps running.
            if (x86::apic_initialize()) {
                current.apic_active = true;
                interrupts::irq_enable(1);
                interrupts::irq_enable(12);
            }
            current.interrupts_ready = interrupts::interrupts_initialized();
            return current.interrupts_ready;
        }
        bool timer_enter(void*) noexcept {
            // Runs after acpi.discover (needs the HPET table) and after
            // interrupts.foundation (enables the PIT-tick calibration
            // fallback). Never fails the boot: the PIT tick path remains
            // usable for sleep/idle even with no HPET and no TSC.
            const bool hires = time::timer_initialize();
            current.hires_time_ready = hires;
            current.time_source = static_cast<hcpp::u8>(time::time_source());
            klog::set_timestamps(true);
            return true;
        }

        RuntimeStage core_stage{"core.services", 10, core_enter, nullptr, StageState::Pending};
        RuntimeStage pci_stage{"pci.scan", 20, pci_enter, nullptr, StageState::Pending};
        RuntimeStage memory_stage{"memory.pmm", 30, memory_enter, nullptr, StageState::Pending};
        RuntimeStage vmm_stage{"memory.vmm", 35, vmm_enter, nullptr, StageState::Pending};
        RuntimeStage acpi_stage{"acpi.discover", 40, acpi_enter, nullptr, StageState::Pending};
        RuntimeStage interrupt_stage{"interrupts.foundation", 50, interrupt_enter, nullptr, StageState::Pending};
        RuntimeStage timer_stage{"time.timer", 55, timer_enter, nullptr, StageState::Pending};
        RuntimeStage smp_stage{"smp.ap", 56, smp_enter, nullptr, StageState::Pending};
        RuntimeStage usb_stage{"usb.xhci", 57, usb_enter, nullptr, StageState::Pending};
        RuntimeStage firewire_stage{"firewire.ohci", 58, firewire_enter, nullptr, StageState::Pending};
        RuntimeStage context_stage{"sched.context", 60, context_enter, nullptr, StageState::Pending};
    }  // namespace

    void initialize() noexcept {
        stage_count = 0;
        current = {};
        is_booted = false;
        for (auto& stage : stages) stage = nullptr;
        register_stage(core_stage);
        register_stage(pci_stage);
        register_stage(memory_stage);
        register_stage(vmm_stage);
        register_stage(acpi_stage);
        register_stage(interrupt_stage);
        register_stage(timer_stage);
        register_stage(smp_stage);
        register_stage(usb_stage);
        register_stage(firewire_stage);
        register_stage(context_stage);
        current.stages_registered = stage_count;
    }

    bool register_stage(RuntimeStage& stage) noexcept {
        if (stage.enter == nullptr || stage_count >= max_stages) return false;
        stage.state = StageState::Pending;
        stages[stage_count++] = &stage;
        return true;
    }

    bool run_stages() noexcept {
        current.stages_completed = current.stages_failed = 0;
        current.stages_registered = stage_count;
        if (stage_count == 0) return false;
        for (hcpp::size_t done = 0; done < stage_count; ++done) {
            RuntimeStage* best = nullptr;
            for (hcpp::size_t i = 0; i < stage_count; ++i) {
                if (stages[i]->state == StageState::Pending &&
                    (best == nullptr || stages[i]->priority < best->priority))
                    best = stages[i];
            }
            if (best == nullptr) break;
            best->state = StageState::Running;
            if (best->enter(best->context)) {
                best->state = StageState::Completed;
                ++current.stages_completed;
            } else {
                best->state = StageState::Failed;
                ++current.stages_failed;
            }
        }
        return current.stages_failed == 0;
    }

    RuntimeInfo boot(const BootInfo& boot_info) noexcept {
        boot_info_pointer = &boot_info;
        initialize();
        const bool stages_ok = run_stages();
        current.core_started = core::services_running();
        current.core_selftest = core::services_self_test();
        is_booted = stages_ok && current.core_started && current.core_selftest && current.memory_initialized &&
                    current.interrupts_ready && current.vmm_selftest && current.context_switch_ready &&
                    current.scheduler_ready;
        return current;
    }

    const RuntimeInfo& info() noexcept { return current; }
    bool booted() noexcept { return is_booted; }

}  // namespace hype::runtime
