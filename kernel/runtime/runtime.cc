#include "kernel/runtime/runtime.h"

#include "kernel/acpi/acpi.h"
#include "kernel/acpi/apic.h"
#include "kernel/acpi/madt.h"
#include "kernel/core_services.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/pci/pci.h"
#include "kernel/sched/context.h"
#include "kernel/sched/scheduler.h"
#include "kernel/video/videoclient.h"
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
        bool context_enter(void*) noexcept {
            current.context_switch_ready = sched::context_selftest();
            sched::scheduler_initialize();
            current.scheduler_ready = sched::scheduler_selftest();
            video::video_client_initialize();
            video::virtual_video_register();
            return current.context_switch_ready && current.scheduler_ready;
        }
        bool interrupt_enter(void*) noexcept {
            interrupts::idt_initialize();
            interrupts::pic_initialize();
            interrupts::pit_initialize(100);
            interrupts::keyboard_initialize();
            interrupts::pic_unmask(0);
            interrupts::pic_unmask(1);
            interrupts::interrupts_enable();
            current.interrupts_ready = interrupts::interrupts_initialized();
            return current.interrupts_ready;
        }

        RuntimeStage core_stage{"core.services", 10, core_enter, nullptr, StageState::Pending};
        RuntimeStage pci_stage{"pci.scan", 20, pci_enter, nullptr, StageState::Pending};
        RuntimeStage memory_stage{"memory.pmm", 30, memory_enter, nullptr, StageState::Pending};
        RuntimeStage vmm_stage{"memory.vmm", 35, vmm_enter, nullptr, StageState::Pending};
        RuntimeStage acpi_stage{"acpi.discover", 40, acpi_enter, nullptr, StageState::Pending};
        RuntimeStage interrupt_stage{"interrupts.foundation", 50, interrupt_enter, nullptr, StageState::Pending};
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
        register_stage(context_stage);
    }

    bool register_stage(RuntimeStage& stage) noexcept {
        if (stage.enter == nullptr || stage_count >= max_stages) return false;
        stage.state = StageState::Pending;
        stages[stage_count++] = &stage;
        return true;
    }

    bool run_stages() noexcept {
        current.stages_completed = current.stages_failed = 0;
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
