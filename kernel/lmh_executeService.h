#pragma once

#include "kernel/lmh_execute.h"
#include "kernel/lmhprocess.h"

namespace hype::lmh {

using ExecuteHandler = hcpp::u32 (*)(void *context,
                                     const ExecuteRequest &request) noexcept;

struct SchedulerStats {
  hcpp::size_t processes;
  hcpp::size_t tasks;
  hcpp::size_t ready_tasks;
  hcpp::size_t completed_tasks;
};

void execute_service_initialize() noexcept;
bool execute_service_register(hcpp::u32 process_id, ExecuteHandler handler,
                              void *context) noexcept;
hcpp::u32 execute_service_run(const ExecuteRequest &request) noexcept;
hcpp::u32 process_create(hcpp::u32 owner_user, hcpp::u32 capabilities) noexcept;
Process *process_find(hcpp::u32 process_id) noexcept;
hcpp::u32 task_create(hcpp::u32 process_id, hcpp::u32 entry, hcpp::u32 argument,
                      hcpp::u8 priority = priority_normal) noexcept;
Task *task_find(hcpp::u32 task_id) noexcept;
hcpp::u32 schedule_next() noexcept;
bool task_complete(hcpp::u32 task_id, hcpp::u32 result) noexcept;
SchedulerStats scheduler_stats() noexcept;
hcpp::size_t execute_service_count() noexcept;
bool execute_service_unregister(hcpp::u32 process_id) noexcept;

} // namespace hype::lmh
