#pragma once
#include "kernel/lmh_execute.h"
namespace hype::lmh {
    using ExecuteHandler = hcpp::u32 (*)(void* context, const ExecuteRequest& request) noexcept;
    void execute_service_initialize() noexcept;
    bool execute_service_register(hcpp::u32 process_id, ExecuteHandler handler, void* context) noexcept;
    hcpp::u32 execute_service_run(const ExecuteRequest& request) noexcept;
}  // namespace hype::lmh
