#pragma once

#include "kernel/lmhtask.h"

namespace hype::lmh {

enum class ProcessState : hcpp::u8 {
  Empty,
  Created,
  Running,
  Blocked,
  Exited,
  Failed
};

struct Process {
  hcpp::u32 id;
  hcpp::u32 parent_id;
  hcpp::u32 owner_user;
  hcpp::u32 capabilities;
  ProcessState state;
  hcpp::u32 task_count;
  hcpp::u32 exit_code;
};

void process_reset(Process &process) noexcept;
bool process_valid(const Process &process) noexcept;
bool process_start(Process &process) noexcept;
void process_exit(Process &process) noexcept;
void process_exit_code(Process &process, hcpp::u32 code) noexcept;
bool process_can_execute(const Process &process, hcpp::u32 required) noexcept;

} // namespace hype::lmh
