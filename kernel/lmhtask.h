#pragma once

#include "hcpp/types.hpp"

namespace hype::lmh {

enum class TaskState : hcpp::u8 {
  Empty,
  Ready,
  Running,
  Waiting,
  Sleeping,
  Finished,
  Failed,
  Zombie
};
constexpr hcpp::u8 priority_high = 0;
constexpr hcpp::u8 priority_normal = 20;
constexpr hcpp::u8 priority_low = 39;

struct Task {
  hcpp::u32 id;
  hcpp::u32 process_id;
  hcpp::u32 parent_id;
  hcpp::u32 entry;
  hcpp::u32 argument;
  hcpp::u8 priority;
  hcpp::u32 time_slice;
  TaskState state;
};

void task_reset(Task &task) noexcept;
bool task_valid(const Task &task) noexcept;
bool task_start(Task &task) noexcept;
void task_finish(Task &task) noexcept;
bool task_fail(Task &task) noexcept;
bool task_wake(Task &task) noexcept;
bool task_sleep(Task &task) noexcept;
bool task_can_transition(TaskState from, TaskState to) noexcept;

} // namespace hype::lmh
