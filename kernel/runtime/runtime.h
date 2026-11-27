#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/boot_info.hpp"

namespace hype::runtime {

    enum class StageState : hcpp::u8 { Pending, Running, Completed, Failed };
    using StageEntry = bool (*)(void *context) noexcept;

    struct RuntimeStage {
        hcpp::StringView name;
        hcpp::u8 priority;
        StageEntry enter;
        void *context;
        StageState state;
    };

    struct RuntimeInfo {
        bool core_started;
        bool core_selftest;
        hcpp::size_t pci_devices;
        bool memory_initialized;
        hcpp::u64 memory_free_bytes;
        hcpp::size_t memory_e820_entries;
        hcpp::size_t memory_usable_regions;
        bool memory_selftest;
        bool vmm_initialized;
        bool vmm_selftest;
        bool acpi_present;
        hcpp::size_t acpi_tables;
        bool madt_present;
        hcpp::size_t apic_cpus;
        hcpp::size_t ioapics;
        bool apic_supported;
        hcpp::uintptr_t apic_base;
        bool apic_active;
        bool interrupts_ready;
        bool context_switch_ready;
        bool scheduler_ready;
        hcpp::size_t stages_registered;
        hcpp::size_t stages_completed;
        hcpp::size_t stages_failed;
    };

    void initialize() noexcept;
    bool register_stage(RuntimeStage &stage) noexcept;
    bool run_stages() noexcept;
    RuntimeInfo boot(const BootInfo &boot_info) noexcept;
    const RuntimeInfo &info() noexcept;
    bool booted() noexcept;

}  // namespace hype::runtime
