#pragma once

#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    const HxerHeader* speedstart_app() noexcept;
    const HxerHeader* hxsettings_app() noexcept;
    const HxerHeader* hxabout_app() noexcept;
    void hxrt_register_builtins() noexcept;

}
