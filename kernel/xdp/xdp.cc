#include "kernel/xdp/xdp.h"

#include "kernel/xdp/xdpint.h"

#include "hcpp/memory.hpp"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/serial.hpp"

// XDP core — device registry, backend ladder, surfaces, cmd dispatch
// and present fanout. See kernel/xdp/xdp.h for the architecture notes.

namespace hype::xdp {

    namespace {

        constexpr hcpp::u32 max_backends = 8;
        constexpr hcpp::u32 max_devices = 32;
        constexpr hcpp::u8 pci_class_display = 0x03;

        const xdp_backend* backends[max_backends];
        hcpp::u32 backend_count = 0;

        xdp_device devices[max_devices];
        hcpp::u32 device_count = 0;

        xdp_scanout scanout;
        xdp_surface compositor;
        bool core_ready = false;
        bool present_logged = false;

        void write_hex16(hcpp::u16 value) noexcept {
            static const char hex[] = "0123456789abcdef";
            for (int shift = 12; shift >= 0; shift -= 4) {
                serial::write_char(hex[(value >> shift) & 0xF]);
            }
        }

        // VGA-class PCI devices not claimed by a real backend become
        // "bare gpu" registry entries — the Vulkan/OpenCL ladder rung
        // that a future native driver would pick up. Serial marker per
        // device: "xdp gpu=<vendor:device> no-driver".
        void enumerate_bare_gpus() noexcept {
            const pci::PciDevice* list = pci::devices();
            const hcpp::size_t n = pci::device_count();
            for (hcpp::size_t i = 0; i < n; ++i) {
                const pci::PciDevice& d = list[i];
                if (d.class_code != pci_class_display ||
                    d.vendor_id == pci::invalid_vendor) {
                    continue;
                }
                bool claimed = false;
                for (hcpp::u32 k = 0; k < device_count; ++k) {
                    if (devices[k].vendor_id == d.vendor_id &&
                        devices[k].device_id == d.device_id &&
                        devices[k].bus == d.bus &&
                        devices[k].slot == d.slot &&
                        devices[k].function == d.function) {
                        claimed = true;
                        break;
                    }
                }
                if (claimed) continue;
                xdp_device_add(XdpDevKind::BareGpu, d.vendor_id,
                               d.device_id, d.bus, d.slot, d.function,
                               nullptr);
                serial::write("xdp gpu=");
                write_hex16(d.vendor_id);
                serial::write_char(':');
                write_hex16(d.device_id);
                serial::write(" no-driver\r\n");
            }
        }

        // Exercises the cmd model end-to-end on a scratch surface:
        // clear -> fill -> stroke -> blit, then verifies landed pixels.
        bool cmd_selftest() noexcept {
            xdp_surface scratch{};
            if (!xdp_surface_create(scratch, 64, 64)) return false;
            xdp_cmdbuf buf{};
            buf.clear(0xFF102030);
            buf.fill_rect(hype::ui::Rect{4, 4, 32, 16}, 0xFF00FF00);
            buf.stroke_rect(hype::ui::Rect{8, 8, 40, 40}, 0xFFFF0000, 2);
            const bool submitted = xdp_submit(scratch, buf);
            bool ok = submitted && !buf.overflow;
            const hcpp::u32* px =
                reinterpret_cast<const hcpp::u32*>(scratch.pixels);
            ok = ok && px[4 * 64 + 4] == 0xFF00FF00;   // inside fill
            ok = ok && px[8 * 64 + 8] == 0xFFFF0000;   // on the stroke
            ok = ok && px[60 * 64 + 60] == 0xFF102030; // clear remainder
            // Cross-surface blit through the same cmd path.
            xdp_surface dst{};
            ok = ok && xdp_surface_create(dst, 32, 32);
            if (ok) {
                buf.reset();
                buf.clear(0);
                buf.blit(scratch, hype::ui::Point{4, 4},
                         hype::ui::Rect{0, 0, 32, 16});
                ok = xdp_submit(dst, buf) && !buf.overflow;
                const hcpp::u32* dpx =
                    reinterpret_cast<const hcpp::u32*>(dst.pixels);
                ok = ok && dpx[0] == 0xFF00FF00;
            }
            xdp_surface_destroy(dst);
            xdp_surface_destroy(scratch);
            return ok;
        }

    }  // namespace

    // ------------------------------------------------------------------
    // Surfaces
    // ------------------------------------------------------------------

    bool xdp_surface_create(xdp_surface& surf, hcpp::u32 width,
                            hcpp::u32 height) noexcept {
        surf = xdp_surface{};
        if (width == 0 || height == 0) return false;
        const hcpp::size_t bytes =
            static_cast<hcpp::size_t>(width) * height * 4;
        const hcpp::size_t pages =
            (bytes + memory::page_size - 1) / memory::page_size;
        const hcpp::uintptr_t phys = memory::pmm_allocate_pages(pages);
        if (phys == 0) return false;
        surf.pixels = reinterpret_cast<hcpp::u8*>(phys);
        surf.phys = phys;
        surf.width = width;
        surf.height = height;
        surf.pitch_bytes = width * 4;
        surf.format = PixelFormat::Bgra32;
        surf.pages = pages;
        hcpp::set(surf.pixels, 0, pages * memory::page_size);
        return true;
    }

    void xdp_surface_destroy(xdp_surface& surf) noexcept {
        if (surf.phys != 0 && surf.pages != 0) {
            memory::pmm_free_pages(surf.phys, surf.pages);
        }
        surf = xdp_surface{};
    }

    hcpp::graphics::Framebuffer xdp_surface_view(
        const xdp_surface& surf) noexcept {
        // R<<16 G<<8 B<<0 in the u32 -> byte order B,G,R,A = BGRA32.
        return hcpp::graphics::Framebuffer(
            surf.pixels, surf.width, surf.height, surf.pitch_bytes, 32,
            8, 16, 8, 8, 8, 0);
    }

    // ------------------------------------------------------------------
    // Command buffer
    // ------------------------------------------------------------------

    bool xdp_cmdbuf::push(const xdp_cmd& cmd) noexcept {
        if (count >= xdp_cmdbuf_capacity) {
            overflow = true;
            return false;
        }
        ops[count++] = cmd;
        return true;
    }

    bool xdp_cmdbuf::clear(hcpp::u32 color) noexcept {
        xdp_cmd c{};
        c.op = xdp_op_clear;
        c.color = color;
        return push(c);
    }

    bool xdp_cmdbuf::fill_rect(hype::ui::Rect rect,
                               hcpp::u32 color) noexcept {
        xdp_cmd c{};
        c.op = xdp_op_fill_rect;
        c.rect = rect;
        c.color = color;
        return push(c);
    }

    bool xdp_cmdbuf::stroke_rect(hype::ui::Rect rect, hcpp::u32 color,
                                 hcpp::u8 thickness) noexcept {
        xdp_cmd c{};
        c.op = xdp_op_stroke_rect;
        c.aux = thickness;
        c.rect = rect;
        c.color = color;
        return push(c);
    }

    bool xdp_cmdbuf::blit(const xdp_surface& src, hype::ui::Point src_pos,
                          hype::ui::Rect dst) noexcept {
        xdp_cmd c{};
        c.op = xdp_op_blit;
        c.src = &src;
        c.src_pos = src_pos;
        c.rect = dst;
        return push(c);
    }

    // ------------------------------------------------------------------
    // Backend registry / dispatch
    // ------------------------------------------------------------------

    bool xdp_backend_register(const xdp_backend* backend) noexcept {
        if (backend == nullptr || backend_count >= max_backends) {
            return false;
        }
        backends[backend_count++] = backend;
        serial::write("xdp backend=");
        serial::write(backend->name);
        serial::write("\r\n");
        return true;
    }

    hcpp::u32 xdp_backend_count() noexcept { return backend_count; }

    const xdp_backend* xdp_backend_at(hcpp::u32 index) noexcept {
        return index < backend_count ? backends[index] : nullptr;
    }

    const xdp_backend* xdp_render_backend() noexcept {
        // Registration order is the ladder: later == higher tier. Walk
        // backwards and pick the first 2D-capable backend; software is
        // registered first so a CAP_2D result always exists.
        for (hcpp::u32 i = backend_count; i > 0; --i) {
            if ((backends[i - 1]->caps & XDP_CAP_2D) != 0 &&
                backends[i - 1]->submit != nullptr) {
                return backends[i - 1];
            }
        }
        return nullptr;
    }

    bool xdp_submit(xdp_surface& surf, xdp_cmdbuf& buf) noexcept {
        const xdp_backend* be = xdp_render_backend();
        if (be == nullptr || buf.count == 0) return false;
        return be->submit(surf, buf.ops, buf.count);
    }

    bool xdp_present(const xdp_surface& surf, const hype::ui::Rect* dirty,
                     hcpp::u32 count) noexcept {
        bool accepted = false;
        for (hcpp::u32 i = 0; i < backend_count; ++i) {
            const xdp_backend* be = backends[i];
            if ((be->caps & XDP_CAP_PRESENT) == 0 || be->present == nullptr) {
                continue;
            }
            accepted = be->present(surf, dirty, count) || accepted;
        }
        if (accepted && !present_logged) {
            present_logged = true;
            serial::write_line("xdp present=ok");
        }
        return accepted;
    }

    // ------------------------------------------------------------------
    // Device registry / scanout
    // ------------------------------------------------------------------

    void xdp_device_add(XdpDevKind kind, hcpp::u16 vendor, hcpp::u16 dev,
                        hcpp::u8 bus, hcpp::u8 slot, hcpp::u8 function,
                        const xdp_backend* backend) noexcept {
        if (device_count >= max_devices) return;
        xdp_device& d = devices[device_count++];
        d.kind = kind;
        d.vendor_id = vendor;
        d.device_id = dev;
        d.bus = bus;
        d.slot = slot;
        d.function = function;
        d.backend = backend;
    }

    hcpp::u32 xdp_device_count() noexcept { return device_count; }

    const xdp_device* xdp_device_at(hcpp::u32 index) noexcept {
        return index < device_count ? &devices[index] : nullptr;
    }

    const xdp_scanout& xdp_scanout_info() noexcept { return scanout; }

    xdp_surface* xdp_compositor_surface() noexcept {
        return compositor.pixels != nullptr ? &compositor : nullptr;
    }

    // ------------------------------------------------------------------
    // Init
    // ------------------------------------------------------------------

    bool xdp_init(volatile hcpp::u8* scanout_base,
                  hcpp::graphics::Framebuffer& fb, hcpp::u8 bpp,
                  hcpp::u8 red_pos, hcpp::u8 green_pos,
                  hcpp::u8 blue_pos) noexcept {
        scanout = xdp_scanout{};
        scanout.fb = fb;
        scanout.base = scanout_base;
        scanout.bound = fb.valid();
        scanout.native_bgra32 =
            bpp == 32 && red_pos == 16 && green_pos == 8 && blue_pos == 0;

        // Probe order: software first (always-works baseline), then the
        // virtio-gpu PCI probe, then bare-GPU enumeration for the
        // Vulkan/OpenCL future ladder.
        xdp_device_add(XdpDevKind::Software, 0, 0, 0, 0, 0,
                       xdp_sw_backend());
        xdp_backend_register(xdp_sw_backend());

        if (const xdp_backend* vg = xdp_virtgpu_backend()) {
            // Record the claimed device (same preference order as the
            // backend's find_gpu: modern id first, then legacy display).
            const pci::PciDevice* list = pci::devices();
            const hcpp::size_t n = pci::device_count();
            const pci::PciDevice* claimed = nullptr;
            for (hcpp::size_t i = 0; i < n && claimed == nullptr; ++i) {
                if (list[i].vendor_id == 0x1AF4 &&
                    list[i].device_id == 0x1050) {
                    claimed = &list[i];
                }
            }
            for (hcpp::size_t i = 0; i < n && claimed == nullptr; ++i) {
                if (list[i].vendor_id == 0x1AF4 &&
                    list[i].device_id == 0x1000 &&
                    list[i].class_code == pci_class_display) {
                    claimed = &list[i];
                }
            }
            if (claimed != nullptr) {
                xdp_device_add(XdpDevKind::VirtioGpu, claimed->vendor_id,
                               claimed->device_id, claimed->bus,
                               claimed->slot, claimed->function, vg);
            }
            xdp_backend_register(vg);
        } else {
            serial::write_line("xdp virtio-gpu=absent");
        }

        enumerate_bare_gpus();

        // Compositor surface: fullscreen BGRA32 backing store that
        // desk_paint renders into before xdp_present flips dirty rects
        // to every present-capable output.
        if (scanout.bound &&
            xdp_surface_create(compositor, fb.width(), fb.height())) {
            serial::write("xdp compositor=");
            serial::write_dec32(compositor.width);
            serial::write_char('x');
            serial::write_dec32(compositor.height);
            serial::write("\r\n");
        } else {
            serial::write_line("xdp compositor=none");
        }

        serial::write("xdp cmdbuf=");
        serial::write_line(cmd_selftest() ? "ok" : "fail");

        core_ready = true;
        serial::write_line("xdp core=ok");
        return true;
    }

    bool xdp_ready() noexcept { return core_ready; }

}  // namespace hype::xdp
