#pragma once

#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    const HxerHeader* speedstart_app() noexcept;
    const HxerHeader* hxsettings_app() noexcept;
    const HxerHeader* hxabout_app() noexcept;
    const HxerHeader* dxbg_app() noexcept;
    const HxerHeader* revdbg_app() noexcept;
    const HxerHeader* gdxfdemo_app() noexcept;
    const HxerHeader* hxshell_app() noexcept;
    const HxerHeader* hxfiles_app() noexcept;
    void hxrt_register_builtins() noexcept;
    // Registers the "HxAbout" XAU window class (kernel/win/xau.cc), so
    // HxAbout windows render/input through a WndProc.
    void hxabout_register_xau() noexcept;
    // Registers the "HxShell" XAU window class so the terminal paints
    // and receives keyboard input through its WndProc.
    void hxshell_register_xau() noexcept;
    // Registers the "HxFiles" XAU class + its wheel-scroll input hook
    // (kernel/hxrt/apps/hxfiles.cc, issue #128).
    void hxfiles_register_xau() noexcept;

    hcpp::size_t dbg_refresh() noexcept;
    bool dbg_select(hcpp::size_t index) noexcept;
    hcpp::size_t dbg_selected() noexcept;
    bool dbg_restart() noexcept;
    bool dbg_stop() noexcept;
    bool dbg_start() noexcept;
    bool dbg_click(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool revdbg_selftest() noexcept;

}
