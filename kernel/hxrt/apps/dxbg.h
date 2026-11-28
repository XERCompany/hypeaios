#pragma once

#include "hcpp/types.hpp"
#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    constexpr hcpp::u32 dxbg_max_wallpapers = 16;

    const HxerHeader* dxbg_app() noexcept;
    bool dxbg_seed_wallpapers() noexcept;
    hcpp::u32 dxbg_wallpaper_count() noexcept;
    const char* dxbg_wallpaper_name(hcpp::u32 index) noexcept;
    bool dxbg_apply(const char* path) noexcept;
    bool dxbg_apply_index(hcpp::u32 index) noexcept;
    // Unified wallpaper spec: "gradient", a procedural design name
    // (aurora/mesh/dusk/mono/nebula), "img:<leaf>" for HYPEOS://U/<leaf>,
    // or a bare leaf name resolved under U/wallpapers then U.
    bool dxbg_apply_spec(const char* name) noexcept;
    bool dxbg_selftest() noexcept;

}
