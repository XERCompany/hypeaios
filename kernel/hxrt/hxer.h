#pragma once

#include "hcpp/types.hpp"
#include "kernel/hxrt/appctx.h"

namespace hype::hxrt {

    constexpr hcpp::u32 hxer_magic = 0x48584552;

    enum class HxerKind : hcpp::u8 { User = 0, System = 1 };

    struct HxerHeader {
        hcpp::u32 magic;
        hcpp::u16 ver;
        hcpp::u8 kind;
        hcpp::u8 flags;
        char name[24];
        char path[64];
        void (*entry)(AppCtx& ctx) noexcept;
        void (*on_close)(AppCtx& ctx) noexcept;
        // Optional content-click handler: invoked by the window manager
        // with absolute screen coords when a press lands inside the
        // window's content region. Deserialized blobs leave it null.
        void (*on_click)(AppCtx& ctx, hcpp::i32 x, hcpp::i32 y) noexcept;
    };

}
