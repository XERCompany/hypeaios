#include "kernel/lmhtask.h"
namespace hype::lmh { void task_reset(Task& task) noexcept {task={};task.state=TaskState::Empty;} bool task_valid(const Task& task) noexcept {return task.id!=0&&task.process_id!=0&&task.state!=TaskState::Empty;} }
