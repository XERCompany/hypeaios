#pragma once
#include "kernel/lmhtask.h"
namespace hype::lmh {
enum class ProcessState : hcpp::u8 { Empty, Created, Running, Exited, Failed };
struct Process {
  hcpp::u32 id;
  hcpp::u32 capabilities;
  ProcessState state;
  hcpp::u32 task_count;
};
void process_reset(Process &process) noexcept;
bool process_valid(const Process &process) noexcept;
} // namespace hype::lmh

namespace hype::lmh {
bool process_start(Process &process) noexcept;
void process_exit(Process &process) noexcept;
bool process_can_execute(const Process &process, hcpp::u32 required) noexcept;
} // namespace hype::lmh
