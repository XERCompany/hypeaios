#include "kernel/immtcore.h"
namespace hype::immt {
namespace {
CoreState current = CoreState::Offline;
}
void initialize() noexcept { current = CoreState::Initialized; }
bool start() noexcept {
  if (current != CoreState::Initialized)
    return false;
  current = CoreState::Running;
  return true;
}
void stop() noexcept {
  if (current == CoreState::Running)
    current = CoreState::Initialized;
}
CoreState state() noexcept { return current; }
} // namespace hype::immt

namespace hype::immt {
bool core_running() noexcept { return current == CoreState::Running; }
void core_fail() noexcept { current = CoreState::Failed; }
} // namespace hype::immt
