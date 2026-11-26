#include "kernel/core_services.h"
#include "hcpp/uorgh.h"
#include "kernel/dialog_dev.h"
#include "kernel/filesystem_quick.h"
#include "kernel/functionemitkeys.h"
#include "kernel/functionemitsimmt_dev.h"
#include "kernel/immtcore.h"
#include "kernel/immtqueryservice.h"
#include "kernel/lmh_executeService.h"
#include "kernel/userservice.h"
#include "kernel/vfilezvich.h"

namespace hype::core {
namespace { bool running; }
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
}
