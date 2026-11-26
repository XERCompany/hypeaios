#include "kernel/immtcore.h"

namespace hype::immt {
namespace {
constexpr hcpp::size_t max_modules = 8;
KernelModule *modules[max_modules];
hcpp::size_t count;
CoreState current = CoreState::Offline;
} // namespace
void initialize() noexcept {
  count = 0;
  for (auto &m : modules)
    m = nullptr;
  current = CoreState::Initialized;
}
bool module_register(KernelModule &module) noexcept {
  if (module.init == nullptr || count >= max_modules)
    return false;
  module.initialized = false;
  modules[count++] = &module;
  return true;
}
bool modules_initialize() noexcept {
  bool again = true;
  while (again) {
    again = false;
    for (hcpp::size_t i = 0; i < count; ++i)
      if (!modules[i]->initialized) {
        KernelModule *best = nullptr;
        for (hcpp::size_t j = 0; j < count; ++j)
          if (!modules[j]->initialized &&
              (best == nullptr || modules[j]->priority < best->priority))
            best = modules[j];
        if (best == nullptr)
          return true;
        if (!best->init(best->context)) {
          current = CoreState::Failed;
          return false;
        }
        best->initialized = true;
        again = true;
      }
  }
  return true;
}
void modules_shutdown_reverse() noexcept {
  for (hcpp::size_t i = count; i > 0; --i)
    if (modules[i - 1]->initialized) {
      if (modules[i - 1]->exit != nullptr)
        modules[i - 1]->exit(modules[i - 1]->context);
      modules[i - 1]->initialized = false;
    }
}
hcpp::size_t module_count() noexcept { return count; }
bool start() noexcept {
  if (current != CoreState::Initialized)
    return false;
  if (!modules_initialize())
    return false;
  current = CoreState::Running;
  return true;
}
void stop() noexcept {
  if (current == CoreState::Running) {
    modules_shutdown_reverse();
    current = CoreState::Initialized;
  }
}
CoreState state() noexcept { return current; }
bool core_running() noexcept { return current == CoreState::Running; }
void core_fail() noexcept { current = CoreState::Failed; }
} // namespace hype::immt
