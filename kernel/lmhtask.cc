#include "kernel/lmhtask.h"

namespace hype::lmh {
void task_reset(Task &task) noexcept {
  task = {};
  task.state = TaskState::Empty;
  task.priority = priority_normal;
}
bool task_valid(const Task &task) noexcept {
  return task.id != 0 && task.process_id != 0 && task.state != TaskState::Empty;
}
bool task_can_transition(TaskState from, TaskState to) noexcept {
  if (from == TaskState::Ready)
    return to == TaskState::Running || to == TaskState::Waiting ||
           to == TaskState::Failed;
  if (from == TaskState::Running)
    return to == TaskState::Ready || to == TaskState::Sleeping ||
           to == TaskState::Finished || to == TaskState::Failed;
  if (from == TaskState::Sleeping || from == TaskState::Waiting)
    return to == TaskState::Ready || to == TaskState::Failed;
  return false;
}
bool task_start(Task &task) noexcept {
  if (!task_can_transition(task.state, TaskState::Running))
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
bool task_wake(Task &task) noexcept {
  if (!task_can_transition(task.state, TaskState::Ready))
    return false;
  task.state = TaskState::Ready;
  return true;
}
bool task_sleep(Task &task) noexcept {
  if (!task_can_transition(task.state, TaskState::Sleeping))
    return false;
  task.state = TaskState::Sleeping;
  return true;
}
} // namespace hype::lmh
