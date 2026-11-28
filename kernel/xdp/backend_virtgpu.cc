#include "kernel/virtio/vring.h"
#include "kernel/xdp/xdpint.h"

#include "hcpp/memory.hpp"
#include "kernel/arch/x86/io.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

// XDP virtio-gpu backend — legacy virtio-pci GPU (virtio device id 16).
//
// PCI ids probed (issue #108): 1AF4:1000 (legacy transitional range —
// accepted only when the PCI class is display/0x03 so virtio-net at the
// same id, class 0x02, is never claimed) and 1AF4:1050 (modern-id
// transitional GPU).
//
// The backend is present-only: virtio-gpu has no guest-side 2D drawing
// commands, so painting still runs through the software backend into
// the surface, then this backend uploads dirty rects into a host-side
// 2D resource with TRANSFER_TO_HOST_2D + FLUSH. VBE stays the visible
// scanout — virtio-gpu owns its own display head (QEMU shows it as a
// second monitor window).
//
// All control traffic is synchronous over controlq (queue 0); cursorq
// (queue 1) is set up but unused in v1.

namespace hype::xdp {

    namespace {

        constexpr hcpp::u16 virtio_vendor = 0x1AF4;
        constexpr hcpp::u16 virtio_gpu_legacy_id = 0x1000;
        constexpr hcpp::u16 virtio_gpu_modern_id = 0x1050;
        constexpr hcpp::u8 pci_class_display = 0x03;

        // controlq / cursorq indices.
        constexpr hcpp::u16 queue_ctrl = 0;
        constexpr hcpp::u16 queue_cursor = 1;

        // virtio_gpu command / response type codes.
        constexpr hcpp::u32 cmd_get_display_info = 0x0100;
        constexpr hcpp::u32 cmd_resource_create_2d = 0x0101;
        constexpr hcpp::u32 cmd_set_scanout = 0x0103;
        constexpr hcpp::u32 cmd_resource_flush = 0x0104;
        constexpr hcpp::u32 cmd_transfer_to_host_2d = 0x0105;
        constexpr hcpp::u32 cmd_resource_attach_backing = 0x0106;
        constexpr hcpp::u32 cmd_get_edid = 0x010A;
        // (unref/detach_backing omitted: resources live for the whole
        // session in v1 — add them when surfaces get hot-plugged.)

        constexpr hcpp::u32 resp_ok_nodata = 0x1100;
        constexpr hcpp::u32 resp_ok_display_info = 0x1101;
        constexpr hcpp::u32 resp_ok_edid = 0x1104;

        constexpr hcpp::u32 format_b8g8r8a8 = 1;  // B8G8R8A8_UNORM
        constexpr hcpp::u32 feature_edid = 1u << 1;  // VIRTIO_GPU_F_EDID

        // Device config offsets past virtio::reg_device_config.
        constexpr hcpp::u16 cfg_events_read = 0;
        constexpr hcpp::u16 cfg_events_clear = 4;
        constexpr hcpp::u16 cfg_num_scanouts = 8;

        struct gpu_ctrl_hdr {
            hcpp::u32 type;
            hcpp::u32 flags;
            hcpp::u64 fence_id;
            hcpp::u32 ctx_id;
            hcpp::u32 padding;
        };
        static_assert(sizeof(gpu_ctrl_hdr) == 24);

        struct gpu_rect {
            hcpp::u32 x, y, width, height;
        };

        struct gpu_display_one {
            gpu_rect r;
            hcpp::u32 enabled;
            hcpp::u32 flags;
        };

        struct gpu_resp_display_info {
            gpu_ctrl_hdr hdr;
            gpu_display_one pmodes[16];
        };

        struct gpu_create_2d {
            gpu_ctrl_hdr hdr;
            hcpp::u32 resource_id;
            hcpp::u32 format;
            hcpp::u32 width;
            hcpp::u32 height;
        };

        struct gpu_set_scanout {
            gpu_ctrl_hdr hdr;
            gpu_rect r;
            hcpp::u32 scanout_id;
            hcpp::u32 resource_id;
        };

        struct gpu_flush {
            gpu_ctrl_hdr hdr;
            hcpp::u32 resource_id;
            hcpp::u32 padding;
            gpu_rect r;
        };

        struct gpu_transfer {
            gpu_ctrl_hdr hdr;
            gpu_rect r;
            hcpp::u64 offset;
            hcpp::u32 resource_id;
            hcpp::u32 padding;
        };

        struct gpu_attach_backing {
            gpu_ctrl_hdr hdr;
            hcpp::u32 resource_id;
            hcpp::u32 nr_entries;
        };

        struct gpu_mem_entry {
            hcpp::u64 addr;
            hcpp::u32 length;
            hcpp::u32 padding;
        };

        struct gpu_get_edid {
            gpu_ctrl_hdr hdr;
            hcpp::u32 scanout;
            hcpp::u32 padding;
        };

        struct gpu_resp_edid {
            gpu_ctrl_hdr hdr;
            hcpp::u32 scanout;
            hcpp::u32 padding;
            hcpp::u32 size;
            hcpp::u8 edid[1024];
        };

        bool present = false;
        hcpp::u16 iobase = 0;
        virtio::Vring ctrlq;
        virtio::Vring cursorq;
        hcpp::u32 num_scanouts = 0;
        gpu_rect display_rect{};

        // Host resource bound to the currently-presented surface.
        hcpp::u32 bound_resource = 0;
        hcpp::uintptr_t bound_phys = 0;
        hcpp::u32 bound_w = 0;
        hcpp::u32 bound_h = 0;

        // DMA bounce buffers — one outstanding control command at a
        // time, synchronous. Identity-mapped BSS is below 4 GiB and
        // page-aligned for PFN-based queue programming to be safe.
        hcpp::u8 req_page[4096] __attribute__((aligned(4096)));
        hcpp::u8 resp_page[4096] __attribute__((aligned(4096)));

        hcpp::uintptr_t page_phys(const void* p) noexcept {
            return reinterpret_cast<hcpp::uintptr_t>(p);
        }

        void write_hex16(hcpp::u16 value) noexcept {
            static const char hex[] = "0123456789abcdef";
            for (int shift = 12; shift >= 0; shift -= 4) {
                serial::write_char(hex[(value >> shift) & 0xF]);
            }
        }

        // Runs one controlq command: header-only or struct payload in
        // req_page, response into resp_page. Returns the response type,
        // or 0 on transport failure.
        hcpp::u32 ctrl_exec(hcpp::u32 type, hcpp::u32 req_len,
                            hcpp::u32 resp_cap) noexcept {
            gpu_ctrl_hdr* hdr =
                reinterpret_cast<gpu_ctrl_hdr*>(req_page);
            hdr->type = type;
            hdr->flags = 0;
            hdr->fence_id = 0;
            hdr->ctx_id = 0;
            hdr->padding = 0;
            hcpp::set(resp_page, 0, resp_cap);

            const hcpp::i32 chain = virtio::vring_desc_alloc(ctrlq, 2);
            if (chain < 0) return 0;
            const hcpp::u16 head = static_cast<hcpp::u16>(chain);
            const hcpp::u16 resp_d = ctrlq.desc[head].next;
            ctrlq.desc[head].addr = page_phys(req_page);
            ctrlq.desc[head].len = req_len;
            ctrlq.desc[head].flags = virtio::desc_flag_next;
            ctrlq.desc[resp_d].addr = page_phys(resp_page);
            ctrlq.desc[resp_d].len = resp_cap;
            ctrlq.desc[resp_d].flags = virtio::desc_flag_write;
            virtio::vring_push_avail(ctrlq, head);
            virtio::vring_notify(iobase, queue_ctrl);

            hcpp::u32 used_id = 0;
            hcpp::u32 used_len = 0;
            const bool ok = virtio::vring_wait_used(ctrlq, used_id, used_len,
                                                    0x2000000);
            virtio::vring_desc_free_chain(ctrlq, head);
            if (!ok) return 0;
            const gpu_ctrl_hdr* resp =
                reinterpret_cast<const gpu_ctrl_hdr*>(resp_page);
            return resp->type;
        }

        const pci::PciDevice* find_gpu() noexcept {
            // Modern-id transitional device first.
            if (const pci::PciDevice* dev =
                    pci::find(virtio_vendor, virtio_gpu_modern_id)) {
                return dev;
            }
            // Legacy id: shared with other transitional devices, so the
            // display class check is what claims the GPU (virtio-net at
            // the same id is class 0x02 and skipped).
            const pci::PciDevice* list = pci::devices();
            const hcpp::size_t n = pci::device_count();
            for (hcpp::size_t i = 0; i < n; ++i) {
                const pci::PciDevice& d = list[i];
                if (d.vendor_id == virtio_vendor &&
                    d.device_id == virtio_gpu_legacy_id &&
                    d.class_code == pci_class_display) {
                    return &d;
                }
            }
            return nullptr;
        }

        bool query_display_info() noexcept {
            const hcpp::u32 type =
                ctrl_exec(cmd_get_display_info, sizeof(gpu_ctrl_hdr),
                          sizeof(gpu_resp_display_info));
            if (type != resp_ok_display_info) return false;
            const gpu_resp_display_info* info =
                reinterpret_cast<const gpu_resp_display_info*>(resp_page);
            for (hcpp::u32 i = 0; i < num_scanouts && i < 16; ++i) {
                if (info->pmodes[i].enabled != 0) {
                    display_rect = info->pmodes[i].r;
                    serial::write("virtio-gpu display=");
                    serial::write_dec32(display_rect.width);
                    serial::write_char('x');
                    serial::write_dec32(display_rect.height);
                    serial::write(" scanout=");
                    serial::write_dec32(i);
                    serial::write("\r\n");
                    return true;
                }
            }
            return false;
        }

        void query_edid() noexcept {
            gpu_get_edid* req =
                reinterpret_cast<gpu_get_edid*>(req_page);
            req->scanout = 0;
            req->padding = 0;
            const hcpp::u32 type = ctrl_exec(cmd_get_edid, sizeof(*req),
                                             sizeof(gpu_resp_edid));
            const gpu_resp_edid* resp =
                reinterpret_cast<const gpu_resp_edid*>(resp_page);
            if (type == resp_ok_edid && resp->size != 0) {
                serial::write("virtio-gpu edid=");
                serial::write_dec32(resp->size);
                serial::write("B ok\r\n");
            } else {
                serial::write_line("virtio-gpu edid=none");
            }
        }

        // Ensures a host 2D resource exists and backs `surf` on
        // scanout 0. Re-creates when the surface identity changes.
        bool ensure_resource(const xdp_surface& surf) noexcept {
            if (bound_resource != 0 && bound_phys == surf.phys &&
                bound_w == surf.width && bound_h == surf.height) {
                return true;
            }
            const hcpp::u32 res_id = 1;
            gpu_create_2d* create =
                reinterpret_cast<gpu_create_2d*>(req_page);
            create->resource_id = res_id;
            create->format = format_b8g8r8a8;
            create->width = surf.width;
            create->height = surf.height;
            if (ctrl_exec(cmd_resource_create_2d, sizeof(*create),
                          sizeof(gpu_ctrl_hdr)) != resp_ok_nodata) {
                return false;
            }

            gpu_attach_backing* attach =
                reinterpret_cast<gpu_attach_backing*>(req_page);
            attach->resource_id = res_id;
            attach->nr_entries = 1;
            gpu_mem_entry* entry =
                reinterpret_cast<gpu_mem_entry*>(req_page +
                                                 sizeof(*attach));
            entry->addr = surf.phys;
            entry->length = surf.pitch_bytes * surf.height;
            entry->padding = 0;
            if (ctrl_exec(cmd_resource_attach_backing,
                          sizeof(*attach) + sizeof(*entry),
                          sizeof(gpu_ctrl_hdr)) != resp_ok_nodata) {
                return false;
            }

            gpu_set_scanout* scan =
                reinterpret_cast<gpu_set_scanout*>(req_page);
            scan->r.x = 0;
            scan->r.y = 0;
            scan->r.width = surf.width;
            scan->r.height = surf.height;
            scan->scanout_id = 0;
            scan->resource_id = res_id;
            if (ctrl_exec(cmd_set_scanout, sizeof(*scan),
                          sizeof(gpu_ctrl_hdr)) != resp_ok_nodata) {
                return false;
            }

            bound_resource = res_id;
            bound_phys = surf.phys;
            bound_w = surf.width;
            bound_h = surf.height;
            return true;
        }

        bool push_rect(const hype::ui::Rect& rect) noexcept {
            gpu_transfer* xfer =
                reinterpret_cast<gpu_transfer*>(req_page);
            xfer->r.x = static_cast<hcpp::u32>(rect.x);
            xfer->r.y = static_cast<hcpp::u32>(rect.y);
            xfer->r.width = static_cast<hcpp::u32>(rect.w);
            xfer->r.height = static_cast<hcpp::u32>(rect.h);
            xfer->offset = 0;
            xfer->resource_id = bound_resource;
            xfer->padding = 0;
            if (ctrl_exec(cmd_transfer_to_host_2d, sizeof(*xfer),
                          sizeof(gpu_ctrl_hdr)) != resp_ok_nodata) {
                return false;
            }
            gpu_flush* flush = reinterpret_cast<gpu_flush*>(req_page);
            flush->resource_id = bound_resource;
            flush->padding = 0;
            flush->r = xfer->r;
            return ctrl_exec(cmd_resource_flush, sizeof(*flush),
                             sizeof(gpu_ctrl_hdr)) == resp_ok_nodata;
        }

        bool virtgpu_present(const xdp_surface& surf,
                             const hype::ui::Rect* dirty,
                             hcpp::u32 count) noexcept {
            if (!present || surf.pixels == nullptr || surf.phys == 0) {
                return false;
            }
            if (!ensure_resource(surf)) return false;
            const hype::ui::Rect bounds{
                0, 0, static_cast<hcpp::i32>(surf.width),
                static_cast<hcpp::i32>(surf.height)};
            bool ok = true;
            for (hcpp::u32 i = 0; i < count; ++i) {
                const hype::ui::Rect r = dirty[i].intersection(bounds);
                if (r.empty()) continue;
                ok = push_rect(r) && ok;
            }
            return ok;
        }

        const xdp_backend backend{
            "virtio-gpu", XDP_CAP_PRESENT | XDP_CAP_REMOTE,
            nullptr, virtgpu_present};

        void fail() noexcept {
            virtio::virtio_status_write(iobase, virtio::status_failed);
            serial::write_line("virtio-gpu dev=fail");
        }

    }  // namespace

    const xdp_backend* xdp_virtgpu_backend() noexcept {
        present = false;
        const pci::PciDevice* dev = find_gpu();
        if (dev == nullptr) return nullptr;

        iobase = 0;
        for (hcpp::u8 i = 0; i < 6; ++i) {
            const hcpp::u32 bar = pci::bar(*dev, i);
            if (pci::bar_is_io(bar)) {
                iobase = pci::bar_io_address(bar);
                break;
            }
        }
        if (iobase == 0) {
            // Modern-only device without a legacy I/O BAR — the modern
            // virtio transport is not implemented; skip gracefully like
            // virtio_net does.
            serial::write_line("virtio-gpu dev=modern-only skipped");
            return nullptr;
        }
        pci::pci_enable_io(*dev);
        pci::pci_enable_busmaster(*dev);

        if (!virtio::virtio_reset(iobase)) {
            serial::write_line("virtio-gpu dev=reset-fail");
            return nullptr;
        }
        using virtio::status_acknowledge;
        using virtio::status_driver;
        using virtio::status_driver_ok;
        using virtio::status_features_ok;
        virtio::virtio_status_write(iobase, status_acknowledge);
        virtio::virtio_status_write(iobase,
                                    status_acknowledge | status_driver);

        // Negotiate only VIRTIO_GPU_F_EDID when offered — no virgl, no
        // blob resources; the 2D control path is all v1 needs.
        const hcpp::u32 features =
            hype_port_in32(iobase + virtio::reg_device_features);
        const hcpp::u32 guest_features = features & feature_edid;
        hype_port_out32(iobase + virtio::reg_guest_features,
                        guest_features);
        virtio::virtio_status_write(
            iobase, status_acknowledge | status_driver | status_features_ok);
        if ((virtio::virtio_status_read(iobase) & status_features_ok) == 0) {
            fail();
            return nullptr;
        }

        num_scanouts = hype_port_in32(
            iobase + virtio::reg_device_config + cfg_num_scanouts);
        if (num_scanouts == 0) num_scanouts = 1;
        // Acknowledge any stale config-change events.
        (void)hype_port_in32(iobase + virtio::reg_device_config +
                             cfg_events_read);
        hype_port_out32(iobase + virtio::reg_device_config +
                            cfg_events_clear,
                        0xFFFFFFFF);

        if (!virtio::vring_setup(ctrlq, iobase, queue_ctrl) ||
            !virtio::vring_setup(cursorq, iobase, queue_cursor)) {
            fail();
            return nullptr;
        }
        virtio::virtio_status_write(
            iobase, status_acknowledge | status_driver |
                        status_features_ok | status_driver_ok);
        present = true;

        query_display_info();
        if ((guest_features & feature_edid) != 0) {
            query_edid();
        }

        serial::write("virtio-gpu dev=ok ");
        write_hex16(dev->vendor_id);
        serial::write_char(':');
        write_hex16(dev->device_id);
        serial::write(" scanouts=");
        serial::write_dec32(num_scanouts);
        serial::write("\r\n");
        return &backend;
    }

}  // namespace hype::xdp
