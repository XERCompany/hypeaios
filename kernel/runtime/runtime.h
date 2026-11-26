#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

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
        hcpp::size_t stages_completed;
        hcpp::size_t stages_failed;
    };

    void initialize() noexcept;
    bool register_stage(RuntimeStage &stage) noexcept;
    bool run_stages() noexcept;
    RuntimeInfo boot() noexcept;
    const RuntimeInfo &info() noexcept;
    bool booted() noexcept;

}  // namespace hype::runtime
