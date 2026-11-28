#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

// XDP — the HypeAI display/render manager (issue #108).
//
// XDP sits between paint producers (compositor, apps, demos) and the
// display hardware ladder:
//
//     producers ──> xdp_cmd buffer ──> backend.submit()  (2D ops)
//     xdp_surface ──> xdp_present() ──> backend.present() (scanout flip)
//
// Backends form a ladder ordered by capability:
//   * software  — executes the cmd buffer against surface pixels and
//                 presents dirty rects into the VBE linear framebuffer.
//                 Always registered; v1 active path.
//   * virtio-gpu — legacy virtio-pci GPU: host-side 2D resource +
//                 transfer_to_host_2d/flush per dirty rect. Present-
//                 capable only; painting still runs through software.
//   * vulkan/opencl — NOT IMPLEMENTED. Real GPUs discovered on the PCI
//                 bus are enumerated into xdp_device[] with no backend
//                 ("xdp gpu=<id> no-driver") and the
//                 xdp_backend_register() hook + XDP_CAP_VULKAN /
//                 XDP_CAP_OPENCL flags mark the extension points for a
//                 future native driver.
//
// All surfaces are BGRA32 (byte order B,G,R,A — 0xAARRGGBB pixel value),
// PMM-backed, identity-mapped so host-visible virtio backing attaches
// without an IOMMU remap.

namespace hype::xdp {

    enum class PixelFormat : hcpp::u8 {
        Bgra32 = 0,  // 4 B/px; pixel u32 = 0xAARRGGBB
    };

    // Backend capability flags. A backend advertises what it can do;
    // the core dispatches submit() to the highest-tier CAP_2D backend
    // and fans present() out to every CAP_PRESENT one.
    enum Cap : hcpp::u32 {
        XDP_CAP_2D = 1u << 0,      // executes the xdp_cmd op set
        XDP_CAP_PRESENT = 1u << 1, // flips surfaces to a visible output
        XDP_CAP_COMPUTE = 1u << 2, // generic compute queue
        XDP_CAP_VULKAN = 1u << 3,  // reserved: future native driver
        XDP_CAP_OPENCL = 1u << 4,  // reserved: future native driver
        XDP_CAP_REMOTE = 1u << 5,  // pixels land host-side (virtio-gpu)
    };

    // ------------------------------------------------------------------
    // Surfaces
    // ------------------------------------------------------------------

    struct xdp_surface {
        hcpp::u8* pixels = nullptr;        // kernel VA (identity-mapped)
        hcpp::uintptr_t phys = 0;          // same pages, phys view for DMA
        hcpp::u32 width = 0;
        hcpp::u32 height = 0;
        hcpp::u32 pitch_bytes = 0;
        PixelFormat format = PixelFormat::Bgra32;
        hcpp::size_t pages = 0;
    };

    bool xdp_surface_create(xdp_surface& surf, hcpp::u32 width,
                            hcpp::u32 height) noexcept;
    void xdp_surface_destroy(xdp_surface& surf) noexcept;

    // Framebuffer view over a surface so Canvas/gfx code can paint into
    // it unchanged (masks match BGRA32: R<<16 G<<8 B<<0).
    hcpp::graphics::Framebuffer xdp_surface_view(
        const xdp_surface& surf) noexcept;

    // ------------------------------------------------------------------
    // Command buffer — compact batched 2D ops a backend translates.
    // ------------------------------------------------------------------

    enum xdp_op : hcpp::u8 {
        xdp_op_nop = 0,
        xdp_op_clear = 1,        // color -> whole surface
        xdp_op_fill_rect = 2,    // color -> rect
        xdp_op_stroke_rect = 3,  // color border, thickness = aux (>=1)
        xdp_op_blit = 4,         // src surface src_pos..+rect -> rect
    };

    struct xdp_cmd {
        hcpp::u8 op = xdp_op_nop;
        hcpp::u8 aux = 0;        // stroke thickness
        hcpp::u16 _pad = 0;
        hype::ui::Rect rect{};   // destination rect (clipped to surface)
        hcpp::u32 color = 0;     // BGRA32
        const xdp_surface* src = nullptr;  // blit source
        hype::ui::Point src_pos{};         // blit source origin
    };

    constexpr hcpp::u32 xdp_cmdbuf_capacity = 64;

    struct xdp_cmdbuf {
        xdp_cmd ops[xdp_cmdbuf_capacity];
        hcpp::u32 count = 0;
        bool overflow = false;

        void reset() noexcept { count = 0; overflow = false; }
        bool push(const xdp_cmd& cmd) noexcept;
        bool clear(hcpp::u32 color) noexcept;
        bool fill_rect(hype::ui::Rect rect, hcpp::u32 color) noexcept;
        bool stroke_rect(hype::ui::Rect rect, hcpp::u32 color,
                         hcpp::u8 thickness = 1) noexcept;
        bool blit(const xdp_surface& src, hype::ui::Point src_pos,
                  hype::ui::Rect dst) noexcept;
    };

    // ------------------------------------------------------------------
    // Backends
    // ------------------------------------------------------------------

    struct xdp_backend {
        const char* name = "";
        hcpp::u32 caps = 0;
        // Executes `count` cmds against `surf`. May be null when the
        // backend has no 2D command translation (e.g. virtio-gpu only
        // presents; rendering falls to the software backend).
        bool (*submit)(xdp_surface& surf, const xdp_cmd* cmds,
                       hcpp::u32 count) noexcept = nullptr;
        // Flips the dirty rects of `surf` to the backend's output. May
        // be null for render-only backends.
        bool (*present)(const xdp_surface& surf,
                        const hype::ui::Rect* dirty,
                        hcpp::u32 count) noexcept = nullptr;
    };

    bool xdp_backend_register(const xdp_backend* backend) noexcept;
    hcpp::u32 xdp_backend_count() noexcept;
    const xdp_backend* xdp_backend_at(hcpp::u32 index) noexcept;
    // Highest-capability CAP_2D backend (the submit() target).
    const xdp_backend* xdp_render_backend() noexcept;

    // ------------------------------------------------------------------
    // Device registry — what XDP found on the bus, in probe order.
    // ------------------------------------------------------------------

    enum class XdpDevKind : hcpp::u8 {
        Software = 0,   // virtual device, always present
        VirtioGpu = 1,  // claimed by the virtio-gpu backend
        BareGpu = 2,    // VGA-class PCI device with no driver yet
    };

    struct xdp_device {
        XdpDevKind kind = XdpDevKind::Software;
        hcpp::u16 vendor_id = 0;
        hcpp::u16 device_id = 0;
        hcpp::u8 bus = 0, slot = 0, function = 0;
        const xdp_backend* backend = nullptr;  // null = "no-driver"
    };

    hcpp::u32 xdp_device_count() noexcept;
    const xdp_device* xdp_device_at(hcpp::u32 index) noexcept;

    // ------------------------------------------------------------------
    // Core
    // ------------------------------------------------------------------

    // Binds the boot scanout (VBE LFB) and walks the backend ladder:
    // registers software, probes virtio-gpu on PCI, then enumerates
    // remaining VGA-class PCI devices as driver-less "bare gpu" entries.
    // `base` is the raw LFB pointer and `bpp`+channel positions describe
    // the scanout encoding so the software present path can take a
    // row-copy fast path when it is already BGRA32-compatible.
    bool xdp_init(volatile hcpp::u8* scanout_base,
                  hcpp::graphics::Framebuffer& scanout,
                  hcpp::u8 bpp, hcpp::u8 red_pos, hcpp::u8 green_pos,
                  hcpp::u8 blue_pos) noexcept;
    bool xdp_ready() noexcept;

    // Dispatch a cmd buffer to the render backend (CAP_2D, best tier).
    bool xdp_submit(xdp_surface& surf, xdp_cmdbuf& buf) noexcept;

    // Fan a surface's dirty rects out to every CAP_PRESENT backend:
    // software copies into the bound LFB, virtio-gpu uploads into its
    // host resource. Returns true when at least one backend accepted.
    bool xdp_present(const xdp_surface& surf, const hype::ui::Rect* dirty,
                     hcpp::u32 count) noexcept;

    // ------------------------------------------------------------------
    // Compositor surface (issue #108 integration point).
    //
    // desk_paint() renders the desktop into this PMM-backed surface
    // instead of the LFB, then flips through xdp_present(). The surface
    // persists across repaints so damage-tracked partial paints keep
    // accumulating a full frame. Returns null before xdp_init() or when
    // PMM could not back a fullscreen surface — callers must fall back
    // to painting the raw framebuffer.
    // ------------------------------------------------------------------

    xdp_surface* xdp_compositor_surface() noexcept;

}  // namespace hype::xdp
