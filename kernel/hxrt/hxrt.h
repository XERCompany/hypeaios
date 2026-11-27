#pragma once

#include "hcpp/types.hpp"
#include "kernel/hxrt/appctx.h"
#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    constexpr hcpp::u32 hxrt_max_apps = 32;

    bool hxrt_register(const HxerHeader* header) noexcept;
    const HxerHeader* hxrt_find(const char* name) noexcept;
    const HxerHeader* const* hxrt_list() noexcept;
    hcpp::u32 hxrt_count() noexcept;
    bool hxrt_exec(const char* name, AppCtx& ctx) noexcept;
    bool hxrt_is_system(hcpp::u32 index) noexcept;

    bool hxrt_selftest() noexcept;
    void hxrt_boot() noexcept;

}
