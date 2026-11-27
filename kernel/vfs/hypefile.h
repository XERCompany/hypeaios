#pragma once

#include "hcpp/types.hpp"
#include "kernel/hxrt/hxer.h"

namespace hype::vfs {

    struct HypefileSelftest {
        bool pack_ok;
        bool file_ok;
        bool guard_ok;
    };

    bool hypefile_install(const hype::hxrt::HxerHeader* header) noexcept;
    bool hypefile_load(const char* path) noexcept;
    hcpp::u32 hypefile_sync() noexcept;
    bool hypefile_selftest(HypefileSelftest& out) noexcept;

}
