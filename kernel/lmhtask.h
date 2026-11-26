#pragma once
#include "hcpp/types.hpp"
namespace hype::lmh {
enum class TaskState : hcpp::u8 {
  Empty,
  Ready,
  Running,
  Waiting,
  Finished,
  Failed
};
struct Task {
  hcpp::u32 id;
  hcpp::u32 process_id;
  hcpp::u32 entry;
  TaskState state;
};
void task_reset(Task &task) noexcept;
bool task_valid(const Task &task) noexcept;
} // namespace hype::lmh

namespace hype::lmh {
bool task_start(Task &task) noexcept;
void task_finish(Task &task) noexcept;
bool task_fail(Task &task) noexcept;
} // namespace hype::lmh
