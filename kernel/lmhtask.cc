#include "kernel/lmhtask.h"
namespace hype::lmh {
void task_reset(Task &task) noexcept {
  task = {};
  task.state = TaskState::Empty;
}
bool task_valid(const Task &task) noexcept {
  return task.id != 0 && task.process_id != 0 && task.state != TaskState::Empty;
}
} // namespace hype::lmh

namespace hype::lmh {
bool task_start(Task &task) noexcept {
  if (task.state != TaskState::Ready)
    return false;
  task.state = TaskState::Running;
  return true;
}
void task_finish(Task &task) noexcept {
  if (task.state == TaskState::Running)
    task.state = TaskState::Finished;
}
bool task_fail(Task &task) noexcept {
  if (!task_valid(task))
    return false;
  task.state = TaskState::Failed;
  return true;
}
} // namespace hype::lmh
