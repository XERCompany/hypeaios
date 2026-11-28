#pragma once

#include "kernel/xdp/xdp.h"

// Internal plumbing between the XDP core (xdp.cc) and its backend
// translation units. Not a public API surface.

namespace hype::xdp {

    // Scanout bound by xdp_init(). Exposed to the software backend so
    // present() can pick a row-copy fast path when the LFB is already
    // BGRA32-compatible.
    struct xdp_scanout {
        hcpp::graphics::Framebuffer fb;
        volatile hcpp::u8* base = nullptr;  // raw LFB for row-copy fast path
        bool bound = false;
        bool native_bgra32 = false;  // 32 bpp, R<<16 G<<8 B<<0
    };

    const xdp_scanout& xdp_scanout_info() noexcept;

    // Backend entry points called by xdp_init() in probe order.
    const xdp_backend* xdp_sw_backend() noexcept;
    // Returns a registered virtio-gpu backend, or null when no device
    // probed (1AF4:1000/1050 display-class) or init failed — graceful.
    const xdp_backend* xdp_virtgpu_backend() noexcept;

    // Records a found device into the registry (probe order preserved).
    void xdp_device_add(XdpDevKind kind, hcpp::u16 vendor, hcpp::u16 dev,
                        hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function,
                        const xdp_backend* backend) noexcept;

}  // namespace hype::xdp
