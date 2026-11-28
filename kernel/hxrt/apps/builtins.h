#pragma once

#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    const HxerHeader* speedstart_app() noexcept;
    const HxerHeader* hxsettings_app() noexcept;
    const HxerHeader* hxabout_app() noexcept;
    const HxerHeader* dxbg_app() noexcept;
    const HxerHeader* revdbg_app() noexcept;
    const HxerHeader* gdxfdemo_app() noexcept;
    void hxrt_register_builtins() noexcept;
    // Registers the "HxAbout" XAU window class (kernel/win/xau.cc), so
    // HxAbout windows render/input through a WndProc.
    void hxabout_register_xau() noexcept;

    hcpp::size_t dbg_refresh() noexcept;
    bool dbg_select(hcpp::size_t index) noexcept;
    hcpp::size_t dbg_selected() noexcept;
    bool dbg_restart() noexcept;
    bool dbg_stop() noexcept;
    bool dbg_start() noexcept;
    bool dbg_click(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool revdbg_selftest() noexcept;

}
