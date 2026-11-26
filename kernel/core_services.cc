#include "kernel/core_services.h"

#include "hcpp/uorgh.h"
#include "kernel/ahost.h"
#include "kernel/apgsecurtiy.h"
#include "kernel/dialog_dev.h"
#include "kernel/filesystem_quick.h"
#include "kernel/functionemitkeys.h"
#include "kernel/functionemitsimmt_dev.h"
#include "kernel/immt_memory.h"
#include "kernel/immtcore.h"
#include "kernel/immtqueryservice.h"
#include "kernel/lmh_executeService.h"
#include "kernel/userservice.h"
#include "kernel/vfilezvich.h"

namespace hype::core {
namespace {
bool running;
}
void initialize_services() noexcept {
  hcpp::uorgh::initialize();
  input::initialize_key_emitter();
  input::initialize_device_emitter();
  input::dialog_initialize();
  immt::initialize();
  immt::query_service_initialize();
  running = immt::start();
  fs::quick_fs_initialize();
  fs::vfile_events_reset();
  users::users_initialize();
  lmh::execute_service_initialize();
}
bool services_running() noexcept { return running; }

bool services_self_test() noexcept {
  void *allocation = immt::memory_allocate(128);
  const bool memory_ok = allocation != nullptr && immt::memory_free(allocation);
  const hcpp::u32 user =
      users::users_create("root", security::capability_sys_admin);
  const hcpp::u32 process =
      lmh::process_create(user, security::capability_execute);
  const hcpp::u32 task = lmh::task_create(process, 1, 0, lmh::priority_normal);
  const bool scheduler_ok = user != 0 && process != 0 && task != 0 &&
                            lmh::schedule_next() == task &&
                            lmh::task_complete(task, 0);
  return memory_ok && scheduler_ok && fs::quick_fs() != nullptr;
}

} // namespace hype::core
