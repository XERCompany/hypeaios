#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::immt {

    enum class CoreState : hcpp::u8 { Offline, Initialized, Running, Failed };
    using ModuleInit = bool (*)(void *context) noexcept;
    using ModuleExit = void (*)(void *context) noexcept;

    struct KernelModule {
        hcpp::StringView name;
        hcpp::u8 priority;
        ModuleInit init;
        ModuleExit exit;
        void *context;
        bool initialized;
    };

    void initialize() noexcept;
    bool module_register(KernelModule &module) noexcept;
    bool modules_initialize() noexcept;
    void modules_shutdown_reverse() noexcept;
    hcpp::size_t module_count() noexcept;
    bool start() noexcept;
    void stop() noexcept;
    CoreState state() noexcept;
    bool core_running() noexcept;
    void core_fail() noexcept;

}  // namespace hype::immt
