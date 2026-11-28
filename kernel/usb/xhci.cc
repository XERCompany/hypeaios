// xHCI (USB 3.x host controller) + class-dispatch hooks for the USB
// class drivers: HID boot-protocol input (usbhid.cc), hubs (usbhub.cc),
// mass-storage BOT (usbmsc.cc) and UAC1 audio (usbuac.cc).
//
// Scope (see xhci.h): PCI class 0x0C/0x03/0x30 probe, controller halt +
// reset, DCBAA/command ring/event ring/interrupter-0 programming, Enable
// Slot / Address Device / Evaluate Context / Configure Endpoint commands,
// EP0 control transfers for enumeration (GET_DESCRIPTOR device/config,
// SET_CONFIGURATION), one interrupt-IN endpoint per HID keyboard
// (protocol 1) or mouse (protocol 2) armed via host_intr_arm() for
// usbhid.cc, bulk OUT/IN endpoint pairs for mass-storage class
// (0x08/0x06/0x50), and hub devices (device/interface class 0x09)
// whose downstream ports are enumerated through usbhub.cc with route
// strings + TT fields in the slot context. UAC1 audio devices (class 01)
// bind through usbuac.cc and use the isoch-OUT path added below
// (Isoch TRBs with SIA + IOC, one TD per 1 ms frame). Single polled
// event ring, no MSI, <= 8 slots (shared by all devices incl. hubs),
// no hot-removal. Devices are enumerated during init; afterwards the
// interrupt-IN rings stay armed and usb_poll() (called from
// input::cursor_poll) turns completion events into usbhid::hid_report()
// calls that feed the same key/ptr counters PS/2 feeds.
//
// The host_* entry points at the bottom of this file (declared in
// xhci_host.h) are the narrow API usbhid.cc/usbhub.cc/usbmsc.cc/
// usbuac.cc use to drive control/bulk/interrupt/isoch transfers,
// endpoint-halt recovery, hub slot-context updates and recursive child
// enumeration without re-implementing TRB mechanics.

#include "kernel/usb/xhci.h"

#include "kernel/usb/usbhid.h"
#include "kernel/usb/usbhub.h"
#include "kernel/usb/usbmsc.h"
#include "kernel/usb/usbuac.h"
#include "kernel/usb/xhci_host.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace hype::usb {

    namespace {

        // -----------------------------------------------------------------
        // xHCI register/TRB constants
        // -----------------------------------------------------------------

        // Capability register byte offsets from BAR0.
        constexpr hcpp::u32 cap_caplength = 0x00;
        constexpr hcpp::u32 cap_hcsparams1 = 0x04;
        constexpr hcpp::u32 cap_hccparams1 = 0x10;
        constexpr hcpp::u32 cap_dboff = 0x14;
        constexpr hcpp::u32 cap_rtsoff = 0x18;

        // Operational register offsets (from cap_base + CAPLENGTH).
        constexpr hcpp::u32 op_usbcmd = 0x00;
        constexpr hcpp::u32 op_usbsts = 0x04;
        constexpr hcpp::u32 op_crcr = 0x18;
        constexpr hcpp::u32 op_dcbaap = 0x30;
        constexpr hcpp::u32 op_config = 0x38;
        constexpr hcpp::u32 op_portsc0 = 0x400;  // + 0x10 * (port - 1)

        constexpr hcpp::u32 usbcmd_rs = 1u << 0;
        constexpr hcpp::u32 usbcmd_hcrst = 1u << 1;
        constexpr hcpp::u32 usbcmd_inte = 1u << 2;

        constexpr hcpp::u32 usbsts_hchalted = 1u << 0;
        constexpr hcpp::u32 usbsts_cnr = 1u << 11;

        // Interrupter-0 register offsets (from rt_base + 0x20).
        constexpr hcpp::u32 intr_iman = 0x00;
        constexpr hcpp::u32 intr_imod = 0x04;
        constexpr hcpp::u32 intr_erstsz = 0x08;
        constexpr hcpp::u32 intr_erstba = 0x10;
        constexpr hcpp::u32 intr_erdp = 0x18;
        constexpr hcpp::u32 erdp_ehb = 1u << 3;

        // PORTSC bits.
        constexpr hcpp::u32 portsc_ccs = 1u << 0;
        constexpr hcpp::u32 portsc_ped = 1u << 1;
        constexpr hcpp::u32 portsc_pr = 1u << 4;
        constexpr hcpp::u32 portsc_pp = 1u << 9;
        constexpr hcpp::u32 portsc_prc = 1u << 21;
        // Write-1-to-clear change bits (17..23) we must never toggle.
        constexpr hcpp::u32 portsc_w1c_mask = 0xFE0000u;

        // TRB types (control dword bits 10..15).
        constexpr hcpp::u8 trb_type_normal = 1;
        constexpr hcpp::u8 trb_type_setup = 2;
        constexpr hcpp::u8 trb_type_data = 3;
        constexpr hcpp::u8 trb_type_status = 4;
        constexpr hcpp::u8 trb_type_isoch = 5;
        constexpr hcpp::u8 trb_type_link = 6;
        constexpr hcpp::u8 trb_type_enable_slot = 9;
        constexpr hcpp::u8 trb_type_address_device = 11;
        constexpr hcpp::u8 trb_type_configure_ep = 12;
        constexpr hcpp::u8 trb_type_evaluate_ctx = 13;
        constexpr hcpp::u8 trb_type_reset_endpoint = 14;
        constexpr hcpp::u8 trb_type_set_tr_dequeue = 16;
        constexpr hcpp::u8 trb_type_transfer_event = 32;
        constexpr hcpp::u8 trb_type_cmd_complete = 33;

        constexpr hcpp::u32 trb_cycle = 1u << 0;
        constexpr hcpp::u32 trb_tc = 1u << 1;
        constexpr hcpp::u32 trb_chain = 1u << 5;
        constexpr hcpp::u32 trb_ioc = 1u << 5;
        constexpr hcpp::u32 trb_idt = 1u << 6;
        constexpr hcpp::u32 trb_dir = 1u << 16;

        constexpr hcpp::u32 trb_trt_none = 0u << 16;
        constexpr hcpp::u32 trb_trt_out = 2u << 16;
        constexpr hcpp::u32 trb_trt_in = 3u << 16;
        // Isoch TRB: SIA (Start Isoch ASAP, control bit 31) lets the xHC
        // pick the frame; TBC/TLBPC stay 0 (one packet per TD).
        constexpr hcpp::u32 trb_sia = 1u << 31;

        constexpr hcpp::u8 code_success = 1;
        constexpr hcpp::u8 code_short_packet = 13;
        // Returned by host_bulk() when no transfer event arrives before
        // the bounded wait expires (not a real xHCI completion code).
        constexpr hcpp::u8 code_wait_timeout = 0xFE;

        constexpr hcpp::u8 ep_type_isoch_out = 1;
        constexpr hcpp::u8 ep_type_bulk_out = 2;
        constexpr hcpp::u8 ep_type_control = 4;
        constexpr hcpp::u8 ep_type_bulk_in = 6;
        constexpr hcpp::u8 ep_type_interrupt_in = 7;

        constexpr hcpp::size_t ring_trbs = 256;        // one 4 KiB page
        constexpr hcpp::size_t ring_link_idx = ring_trbs - 1;
        constexpr hcpp::u32 max_slots = 8;
        constexpr hcpp::u32 max_ports = 32;
        constexpr hcpp::u64 wait_command_ticks = 300;  // PIT @100 Hz -> ~3 s
        constexpr hcpp::u64 wait_reset_ticks = 300;
        constexpr hcpp::u64 wait_transfer_ticks = 200;
        constexpr hcpp::u64 wait_halt_ticks = 200;
        constexpr hcpp::u32 ctrl_buf_size = 512;

        struct Trb {
            hcpp::u32 param_lo;
            hcpp::u32 param_hi;
            hcpp::u32 status;
            hcpp::u32 control;
        };
        static_assert(sizeof(Trb) == 16);

        hcpp::u8 trb_type(const Trb &t) noexcept {
            return static_cast<hcpp::u8>((t.control >> 10) & 0x3F);
        }
        hcpp::u8 trb_code(const Trb &t) noexcept {
            return static_cast<hcpp::u8>((t.status >> 24) & 0xFF);
        }
        hcpp::u8 trb_slot(const Trb &t) noexcept {
            return static_cast<hcpp::u8>((t.control >> 24) & 0xFF);
        }
        hcpp::u8 trb_endpoint(const Trb &t) noexcept {
            return static_cast<hcpp::u8>((t.control >> 16) & 0x1F);
        }
        hcpp::u64 trb_param(const Trb &t) noexcept {
            return (hcpp::u64(t.param_hi) << 32) | t.param_lo;
        }

        struct ErstEntry {
            hcpp::u64 address;
            hcpp::u32 size;
            hcpp::u32 reserved;
        };
        static_assert(sizeof(ErstEntry) == 16);

        // -----------------------------------------------------------------
        // Driver state
        // -----------------------------------------------------------------

        struct Ring {
            volatile Trb *trbs;
            hcpp::u64 phys;
            hcpp::u32 enqueue;
            bool pcs;
        };

        enum class DevKind : hcpp::u8 {
            None = 0,
            Keyboard = 1,
            Mouse = 2,
            Other = 3,
            Hub = 4,
            Mass = 5,
            Audio = 6,
        };

    }  // namespace

    // Defined at hype::usb scope so it matches the forward declaration in
    // xhci_host.h: usbhub.cc/usbmsc.cc only ever see it as the opaque
    // HostDevice handle the host_* API passes around.
    struct Device {
        bool used;
        hcpp::u8 slot_id;
        hcpp::u8 port;        // local port number (root port or parent-hub downstream port)
        hcpp::u8 root_port;   // root-hub port the whole topology hangs off
        hcpp::u32 route;      // xHCI route string (0 for root-port devices)
        hcpp::u8 speed;
        hcpp::u8 depth;       // hub tiers above this device
        hcpp::u8 iface;
        hcpp::u8 dev_class;   // bDeviceClass from the device descriptor
        // TT routing for LS/FS devices behind a high-speed hub.
        hcpp::u8 tt_hub_slot;
        hcpp::u8 tt_hub_port;
        // Hub-device slot-context fields (usbhub fills via host_set_hub_context).
        bool is_hub;
        hcpp::u8 hub_ports;
        hcpp::u8 ttt;
        hcpp::u8 ep_dci;
        hcpp::u16 ep_mps;
        hcpp::u8 ep_interval;
        DevKind kind;
        hcpp::u64 dev_ctx_phys;
        hcpp::u64 input_ctx_phys;
        hcpp::u64 report_phys;
        hcpp::u32 report_len;
        Ring ep0;
        Ring intr;
        // Mass-storage bulk endpoints (usbmsc).
        hcpp::u8 bulk_out_dci;
        hcpp::u8 bulk_in_dci;
        hcpp::u8 bulk_out_ep;
        hcpp::u8 bulk_in_ep;
        hcpp::u16 bulk_out_mps;
        hcpp::u16 bulk_in_mps;
        Ring bulk_out;
        Ring bulk_in;
        // UAC isochronous-OUT endpoint (usbuac).
        hcpp::u8 iso_dci;
        hcpp::u16 iso_mps;
        Ring iso;
    };

    namespace {

        hcpp::uintptr_t mmio;
        hcpp::uintptr_t op_base;
        hcpp::uintptr_t rt_base;
        hcpp::uintptr_t db_base;
        hcpp::u32 ctx_size;
        hcpp::u32 hc_max_slots;
        hcpp::u32 hc_max_ports;
        hcpp::u32 slots_enabled;

        hcpp::u64 dcbaa_phys;
        volatile hcpp::u64 *dcbaa;
        Ring cmd_ring;
        volatile Trb *evt_trbs;
        hcpp::u64 evt_ring_phys;
        hcpp::u64 erst_phys;
        volatile ErstEntry *erst;
        hcpp::u32 evt_dequeue;
        bool evt_ccs;
        hcpp::u64 ctrl_buf_phys;
        volatile hcpp::u8 *ctrl_buf;
        hcpp::u64 input_ctx_phys;  // shared scratch input-context page

        Device devices[max_slots];
        bool present;
        bool running;
        hcpp::u8 fail_step;

        // Last events seen by wait_event() matching.
        Trb last_cmd_event;
        bool have_cmd_event;
        Trb last_xfer_event;
        bool have_xfer_event;

        // -----------------------------------------------------------------
        // Small helpers
        // -----------------------------------------------------------------

        volatile hcpp::u32 &cap32(hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(mmio + off);
        }
        volatile hcpp::u32 &op32(hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(op_base + off);
        }
        volatile hcpp::u32 &intr32(hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(rt_base + 0x20 + off);
        }
        volatile hcpp::u32 &doorbell(hcpp::u32 slot) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(db_base + slot * 4);
        }
        volatile hcpp::u32 &portsc(hcpp::u32 port) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(op_base + op_portsc0 +
                                                          (port - 1) * 0x10);
        }
        void op_write64(hcpp::u32 off, hcpp::u64 value) noexcept {
            op32(off) = static_cast<hcpp::u32>(value);
            op32(off + 4) = static_cast<hcpp::u32>(value >> 32);
        }
        void intr_write64(hcpp::u32 off, hcpp::u64 value) noexcept {
            intr32(off) = static_cast<hcpp::u32>(value);
            intr32(off + 4) = static_cast<hcpp::u32>(value >> 32);
        }
        void barrier() noexcept { asm volatile("" ::: "memory"); }

        void zero_bytes(volatile hcpp::u8 *p, hcpp::size_t n) noexcept {
            for (hcpp::size_t i = 0; i < n; ++i) p[i] = 0;
        }
        void zero_page(hcpp::u64 phys) noexcept {
            zero_bytes(reinterpret_cast<volatile hcpp::u8 *>(phys),
                       memory::page_size);
        }

        hcpp::u64 alloc_page() noexcept {
            const hcpp::uintptr_t p = memory::pmm_allocate_page();
            if (p != 0) zero_page(p);
            return p;
        }

        bool wait_flag(volatile hcpp::u32 &reg, hcpp::u32 mask, bool set,
                       hcpp::u64 timeout_ticks) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            while (((reg & mask) != 0) != set) {
                if (interrupts::pit_ticks() - start > timeout_ticks) return false;
                hype_cpu_halt();
            }
            return true;
        }

        // -----------------------------------------------------------------
        // Producer rings (command + per-endpoint transfer rings)
        // -----------------------------------------------------------------

        bool ring_init(Ring &ring) noexcept {
            ring.phys = alloc_page();
            if (ring.phys == 0) return false;
            ring.trbs = reinterpret_cast<volatile Trb *>(ring.phys);
            ring.enqueue = 0;
            ring.pcs = true;
            return true;
        }

        // Enqueue one TRB at the producer position, writing the Link TRB on
        // wrap-around. Returns the physical address of the TRB written.
        hcpp::u64 ring_push(Ring &ring, hcpp::u64 param, hcpp::u32 status,
                            hcpp::u32 control) noexcept {
            const hcpp::u32 idx = ring.enqueue;
            volatile Trb &t = ring.trbs[idx];
            const hcpp::u64 phys = ring.phys + idx * sizeof(Trb);
            t.param_lo = static_cast<hcpp::u32>(param);
            t.param_hi = static_cast<hcpp::u32>(param >> 32);
            t.status = status;
            barrier();
            t.control = control | (ring.pcs ? trb_cycle : 0u);
            barrier();
            ring.enqueue = idx + 1;
            if (ring.enqueue >= ring_link_idx) {
                volatile Trb &link = ring.trbs[ring_link_idx];
                link.param_lo = static_cast<hcpp::u32>(ring.phys);
                link.param_hi = static_cast<hcpp::u32>(ring.phys >> 32);
                link.status = 0;
                barrier();
                link.control = (hcpp::u32(trb_type_link) << 10) | trb_tc |
                               (ring.pcs ? trb_cycle : 0u);
                ring.enqueue = 0;
                ring.pcs = !ring.pcs;
            }
            return phys;
        }

        // -----------------------------------------------------------------
        // Event ring (single, polled)
        // -----------------------------------------------------------------

        void evt_write_erdp() noexcept {
            intr_write64(intr_erdp,
                         (evt_ring_phys + evt_dequeue * sizeof(Trb)) | erdp_ehb);
        }

        bool evt_pop(Trb &out) noexcept {
            volatile Trb &t = evt_trbs[evt_dequeue];
            if (((t.control & trb_cycle) != 0) != evt_ccs) return false;
            out.param_lo = t.param_lo;
            out.param_hi = t.param_hi;
            out.status = t.status;
            out.control = t.control;
            evt_dequeue = evt_dequeue + 1;
            if (evt_dequeue >= ring_trbs) {
                evt_dequeue = 0;
                evt_ccs = !evt_ccs;
            }
            evt_write_erdp();
            return true;
        }

        Device *device_for_slot(hcpp::u8 slot) noexcept {
            for (auto &d : devices)
                if (d.used && d.slot_id == slot) return &d;
            return nullptr;
        }

        void ring_doorbell(hcpp::u32 slot, hcpp::u32 target) noexcept {
            barrier();
            doorbell(slot) = target;
        }

        void handle_transfer_event(const Trb &t) noexcept;

        void dispatch_event(const Trb &t) noexcept {
            const hcpp::u8 type = trb_type(t);
            if (type == trb_type_cmd_complete) {
                last_cmd_event = t;
                have_cmd_event = true;
            } else if (type == trb_type_transfer_event) {
                last_xfer_event = t;
                have_xfer_event = true;
                handle_transfer_event(t);
            }
            // Port-status/other events are ignored: enumeration reads
            // PORTSC directly.
        }

        // Wait for a command-completion (command=true) or transfer event
        // whose param matches the TRB physical address we posted. Other
        // events encountered meanwhile are dispatched normally.
        bool wait_event(hcpp::u64 param_phys, bool command,
                        hcpp::u64 timeout_ticks, Trb &out) noexcept {
            // Commands are serialized, so any still-flagged event belongs
            // to an older wait and can never match this one.
            have_cmd_event = false;
            have_xfer_event = false;
            const hcpp::u64 start = interrupts::pit_ticks();
            for (;;) {
                Trb t{};
                while (evt_pop(t)) {
                    dispatch_event(t);
                    // Check the match right after each dispatch: a later
                    // event in the same drain (e.g. a HID interrupt-IN
                    // completion) overwrites last_xfer_event, so the match
                    // must be tested before the next evt_pop().
                    if (command && have_cmd_event &&
                        trb_param(last_cmd_event) == param_phys) {
                        out = last_cmd_event;
                        have_cmd_event = false;
                        return true;
                    }
                    if (!command && have_xfer_event &&
                        trb_param(last_xfer_event) == param_phys) {
                        out = last_xfer_event;
                        have_xfer_event = false;
                        return true;
                    }
                }
                if (interrupts::pit_ticks() - start > timeout_ticks) return false;
                hype_cpu_halt();
            }
        }

        // -----------------------------------------------------------------
        // Commands + control transfers
        // -----------------------------------------------------------------

        bool run_command(hcpp::u8 type, hcpp::u64 param, hcpp::u32 flags,
                         hcpp::u8 slot_id, hcpp::u8 *completion_code,
                         hcpp::u8 *result_slot = nullptr) noexcept {
            const hcpp::u64 phys =
                ring_push(cmd_ring, param, 0,
                          (hcpp::u32(type) << 10) | flags |
                              (hcpp::u32(slot_id) << 24));
            ring_doorbell(0, 0);
            Trb ev{};
            if (!wait_event(phys, true, wait_command_ticks, ev)) return false;
            if (completion_code != nullptr) *completion_code = trb_code(ev);
            if (result_slot != nullptr) *result_slot = trb_slot(ev);
            return true;
        }

        // Enqueue setup + optional data + status stage on EP0 and wait for
        // the status-stage Transfer Event.
        bool control_transfer(Device &dev, hcpp::u8 bm_request_type,
                              hcpp::u8 b_request, hcpp::u16 w_value,
                              hcpp::u16 w_index, hcpp::u16 w_length,
                              hcpp::u64 data_phys, bool dir_in,
                              hcpp::u8 *code_out = nullptr) noexcept {
            hcpp::u64 setup_param = 0;
            hcpp::u8 *sp = reinterpret_cast<hcpp::u8 *>(&setup_param);
            sp[0] = bm_request_type;
            sp[1] = b_request;
            sp[2] = static_cast<hcpp::u8>(w_value);
            sp[3] = static_cast<hcpp::u8>(w_value >> 8);
            sp[4] = static_cast<hcpp::u8>(w_index);
            sp[5] = static_cast<hcpp::u8>(w_index >> 8);
            sp[6] = static_cast<hcpp::u8>(w_length);
            sp[7] = static_cast<hcpp::u8>(w_length >> 8);

            const bool has_data = w_length != 0;
            const hcpp::u32 trt = !has_data ? trb_trt_none
                                            : (dir_in ? trb_trt_in : trb_trt_out);
            ring_push(dev.ep0, setup_param, 8,
                      (hcpp::u32(trb_type_setup) << 10) | trb_idt | trt |
                          trb_chain);
            if (has_data) {
                ring_push(dev.ep0, data_phys, w_length,
                          (hcpp::u32(trb_type_data) << 10) | trb_chain |
                              (dir_in ? trb_dir : 0u));
            }
            // Status stage direction is the inverse of the data stage; a
            // no-data control transfer ends with an IN status stage.
            const hcpp::u32 status_dir =
                (has_data && dir_in) ? 0u : trb_dir;
            const hcpp::u64 status_phys =
                ring_push(dev.ep0, 0, 0,
                          (hcpp::u32(trb_type_status) << 10) | trb_ioc |
                              status_dir);
            ring_doorbell(dev.slot_id, 1);
            Trb ev{};
            if (!wait_event(status_phys, false, wait_transfer_ticks, ev)) {
                return false;
            }
            const hcpp::u8 code = trb_code(ev);
            if (code_out != nullptr) *code_out = code;
            return code == code_success || code == code_short_packet;
        }

        bool get_descriptor(Device &dev, hcpp::u8 dtype, hcpp::u8 index,
                            hcpp::u16 length, hcpp::u8 *code = nullptr) noexcept {
            zero_bytes(ctrl_buf, length);
            return control_transfer(dev, 0x80, 6,
                                    static_cast<hcpp::u16>((hcpp::u16(dtype) << 8) |
                                                           index),
                                    0, length, ctrl_buf_phys, true, code);
        }
        bool set_configuration(Device &dev, hcpp::u16 config_value) noexcept {
            return control_transfer(dev, 0x00, 9, config_value, 0, 0, 0, false);
        }
        // -----------------------------------------------------------------
        // Input contexts
        // -----------------------------------------------------------------

        volatile hcpp::u32 *ctx_dwords(hcpp::u64 ctx_base, hcpp::u32 index) noexcept {
            return reinterpret_cast<volatile hcpp::u32 *>(ctx_base +
                                                          index * ctx_size);
        }

        void fill_slot_ctx(hcpp::u64 ctx_base, const Device &dev,
                           hcpp::u32 context_entries) noexcept {
            volatile hcpp::u32 *s = ctx_dwords(ctx_base, 1);
            // dw0: route string (19:0), speed (23:20), Hub flag (26),
            // context entries (31:27). dw1: root hub port (23:16) +
            // downstream port count for hubs (31:24). dw2: TT hub
            // slot/port (LS/FS behind a HS hub) + TT think time for hub
            // devices; interrupter target stays 0.
            s[0] = (dev.route & 0xFFFFFu) | (hcpp::u32(dev.speed) << 20) |
                   (dev.is_hub ? (1u << 26) : 0u) | (context_entries << 27);
            s[1] = (hcpp::u32(dev.root_port) << 16) |
                   (hcpp::u32(dev.hub_ports) << 24);
            s[2] = hcpp::u32(dev.tt_hub_slot) |
                   (hcpp::u32(dev.tt_hub_port) << 8) |
                   (hcpp::u32(dev.ttt) << 16);
        }

        void fill_ep_ctx(hcpp::u64 ctx_base, hcpp::u32 ctx_index,
                         hcpp::u8 ep_type, hcpp::u16 mps, hcpp::u8 interval,
                         hcpp::u64 ring_phys, hcpp::u32 avg_trb_len,
                         hcpp::u16 max_esit) noexcept {
            volatile hcpp::u32 *e = ctx_dwords(ctx_base, ctx_index);
            e[0] = hcpp::u32(interval) << 16;
            e[1] = (hcpp::u32(3) << 1) | (hcpp::u32(ep_type) << 3) |
                   (hcpp::u32(mps) << 16);
            e[2] = static_cast<hcpp::u32>(ring_phys) | 1u;  // DCS = 1
            e[3] = static_cast<hcpp::u32>(ring_phys >> 32);
            e[4] = avg_trb_len | (hcpp::u32(max_esit) << 16);
        }

        // -----------------------------------------------------------------
        // Interrupt-IN report polls
        // -----------------------------------------------------------------

        void post_intr_trb(Device &dev) noexcept {
            ring_push(dev.intr, dev.report_phys, dev.ep_mps,
                      (hcpp::u32(trb_type_normal) << 10) | trb_ioc);
            ring_doorbell(dev.slot_id, dev.ep_dci);
        }

        void handle_transfer_event(const Trb &t) noexcept {
            Device *dev = device_for_slot(trb_slot(t));
            if (dev == nullptr) return;
            if (dev->kind == DevKind::Audio) {
                // UAC isoch-OUT completions: usbuac counts them while a
                // bounded stream burst is in flight; the TDs are not
                // reposted here (each burst is self-contained).
                if (trb_endpoint(t) == dev->iso_dci) {
                    usbuac::uac_iso_done(dev, trb_code(t));
                }
                return;
            }
            if (trb_endpoint(t) != dev->ep_dci ||
                (dev->kind != DevKind::Keyboard && dev->kind != DevKind::Mouse)) {
                return;  // EP0 events are consumed by wait_event() above.
            }
            const hcpp::u8 code = trb_code(t);
            if (code == code_success || code == code_short_packet) {
                usbhid::hid_report(
                    dev,
                    reinterpret_cast<const volatile hcpp::u8 *>(
                        dev->report_phys),
                    dev->report_len);
            }
            post_intr_trb(*dev);  // repost the report poll
        }

        // -----------------------------------------------------------------
        // Enumeration
        // -----------------------------------------------------------------

        struct ClassScan {
            hcpp::u8 config_value;
            // Boot-protocol HID interface (protocol 1 = kbd, 2 = mouse).
            bool hid_found;
            hcpp::u8 iface;
            hcpp::u8 protocol;
            hcpp::u8 ep_num;
            hcpp::u16 ep_mps;
            hcpp::u8 ep_interval;
            // Mass-storage bulk-only interface (class 08/sub 06/proto 50).
            bool msc_found;
            hcpp::u8 msc_iface;
            hcpp::u8 msc_out_ep;
            hcpp::u8 msc_in_ep;
            hcpp::u16 msc_out_mps;
            hcpp::u16 msc_in_mps;
            // Hub (interface class 09).
            bool hub_found;
            hcpp::u8 hub_iface;
            // USB Audio (UAC1): AudioControl interface (class 01 /
            // subclass 01) plus an AudioStreaming interface (01/02)
            // carrying at least one isoch-OUT endpoint across its alt
            // settings. Format/alt-setting parsing is usbuac.cc's job.
            bool uac_found;
            bool uac_ac_seen;
            bool uac_as_seen;
            bool uac_iso_out;
            hcpp::u8 uac_ac_iface;
            hcpp::u8 uac_as_iface;
        };

        // Walk a fetched config descriptor, recording the first
        // boot-protocol HID interface with an interrupt-IN endpoint, the
        // first BOT mass-storage interface's bulk OUT/IN pair, and any hub
        // interface. Each endpoint descriptor is attributed to the most
        // recently parsed interface.
        ClassScan scan_config(const volatile hcpp::u8 *d, hcpp::u32 len) noexcept {
            ClassScan out{};
            if (len < 9 || d[1] != 2) return out;
            out.config_value = d[5];
            hcpp::u8 cls = 0;
            hcpp::u8 sub = 0;
            hcpp::u8 proto = 0;
            hcpp::u8 iface_num = 0;
            hcpp::u32 i = d[0] == 0 ? 9 : d[0];
            while (i + 1 < len) {
                const hcpp::u8 blen = d[i];
                const hcpp::u8 btype = d[i + 1];
                if (blen < 2) break;
                if (btype == 4 && i + 8 < len) {  // interface descriptor
                    cls = d[i + 5];
                    sub = d[i + 6];
                    proto = d[i + 7];
                    iface_num = d[i + 2];
                    if (cls == 9 && !out.hub_found) {
                        out.hub_found = true;
                        out.hub_iface = iface_num;
                    }
                    if (cls == 1 && sub == 1 && !out.uac_ac_seen) {
                        out.uac_ac_seen = true;
                        out.uac_ac_iface = iface_num;
                    }
                    if (cls == 1 && sub == 2 && !out.uac_as_seen) {
                        out.uac_as_seen = true;
                        out.uac_as_iface = iface_num;
                    }
                } else if (btype == 5 && i + 6 < len) {  // endpoint
                    const hcpp::u8 addr = d[i + 2];
                    const hcpp::u8 attr = d[i + 3] & 0x03;
                    const hcpp::u16 mps = static_cast<hcpp::u16>(
                        d[i + 4] | (hcpp::u16(d[i + 5]) << 8));
                    if ((addr & 0x80) != 0 && attr == 3 && cls == 3 &&
                        (proto == 1 || proto == 2) && !out.hid_found) {
                        out.hid_found = true;
                        out.iface = iface_num;
                        out.protocol = proto;
                        out.ep_num = static_cast<hcpp::u8>(addr & 0x0F);
                        out.ep_mps = mps;
                        out.ep_interval = d[i + 6];
                    } else if (attr == 2 && cls == 8 && sub == 6 &&
                               proto == 0x50) {
                        // Bulk endpoint on a BOT interface.
                        out.msc_iface = iface_num;
                        if ((addr & 0x80) != 0) {
                            out.msc_in_ep = static_cast<hcpp::u8>(addr & 0x0F);
                            out.msc_in_mps = mps;
                        } else {
                            out.msc_out_ep = static_cast<hcpp::u8>(addr & 0x0F);
                            out.msc_out_mps = mps;
                        }
                    } else if (attr == 1 && cls == 1 && sub == 2 &&
                               (addr & 0x80) == 0) {
                        // Isoch-OUT endpoint on an AudioStreaming
                        // interface (any alt setting).
                        out.uac_iso_out = true;
                    }
                }
                i = static_cast<hcpp::u32>(i + blen);
            }
            out.msc_found = out.msc_in_ep != 0 && out.msc_out_ep != 0;
            out.uac_found =
                out.uac_ac_seen && out.uac_as_seen && out.uac_iso_out;
            return out;
        }

        hcpp::u16 default_mps0(hcpp::u8 speed) noexcept {
            // PORTSC speed: 1=full, 2=low, 3=high, 4+=super.
            if (speed >= 4) return 512;
            if (speed == 3) return 64;
            return 8;
        }

        // xHCI endpoint-context Interval encoding for LS/FS interrupt
        // endpoints: period = 2^(interval-1) microframes; bInterval is ms.
        hcpp::u8 ep_interval_encoding(hcpp::u8 binterval_ms) noexcept {
            hcpp::u32 uf = hcpp::u32(binterval_ms == 0 ? 1 : binterval_ms) * 8;
            hcpp::u8 v = 1;
            for (hcpp::u32 span = 1; span < uf && v < 15; span <<= 1) ++v;
            return v;
        }

        bool port_reset(hcpp::u32 port) noexcept {
            volatile hcpp::u32 &p = portsc(port);
            hcpp::u32 cur = p;
            if ((cur & portsc_pp) == 0) {
                p = (cur & ~portsc_w1c_mask) | portsc_pp;
                wait_flag(p, portsc_pp, true, wait_reset_ticks);
            }
            cur = p;
            if ((cur & portsc_ccs) == 0) return false;
            if ((cur & portsc_ped) != 0) return true;  // SS: already enabled
            p = (cur & ~portsc_w1c_mask) | portsc_pr;
            if (!wait_flag(p, portsc_prc, true, wait_reset_ticks)) return false;
            cur = p;
            p = cur | portsc_prc;  // ack the port-reset-change bit
            cur = p;
            return (cur & portsc_ccs) != 0 && (cur & portsc_ped) != 0;
        }

        Device *free_device() noexcept {
            for (auto &d : devices)
                if (!d.used) return &d;
            return nullptr;
        }

        // Enable a slot, address the device and bind a class driver.
        // local_port is informational (root port or downstream port on the
        // parent hub); root_port/route/tt_* describe the topology used in
        // the slot context.
        bool enumerate_device(hcpp::u8 local_port, hcpp::u8 speed,
                              hcpp::u8 root_port, hcpp::u32 route,
                              hcpp::u8 tt_hub_slot, hcpp::u8 tt_hub_port,
                              hcpp::u8 depth) noexcept {
            Device *dev = free_device();
            if (dev == nullptr) return true;  // table full: ignore extra ports

            *dev = Device{};
            dev->used = true;  // claim the entry; kind stays None on failure
            dev->port = local_port;
            dev->root_port = root_port;
            dev->route = route;
            dev->tt_hub_slot = tt_hub_slot;
            dev->tt_hub_port = tt_hub_port;
            dev->depth = depth;
            dev->speed = speed;
            dev->kind = DevKind::None;

            hcpp::u8 code = 0;
            hcpp::u8 slot = 0;
            if (!run_command(trb_type_enable_slot, 0, 0, 0, &code, &slot) ||
                code != code_success || slot == 0) {
                fail_step = 10;
                return false;
            }
            dev->slot_id = slot;

            dev->dev_ctx_phys = alloc_page();
            dev->report_phys = alloc_page();
            if (dev->dev_ctx_phys == 0 || dev->report_phys == 0 ||
                !ring_init(dev->ep0)) {
                fail_step = 11;
                return false;
            }
            dcbaa[slot] = dev->dev_ctx_phys;
            barrier();

            // Address Device: input ctx with A0 (slot) + A1 (EP0) added.
            zero_page(input_ctx_phys);
            volatile hcpp::u32 *ctl = ctx_dwords(input_ctx_phys, 0);
            ctl[1] = (1u << 0) | (1u << 1);  // add slot ctx + EP0 ctx
            fill_slot_ctx(input_ctx_phys, *dev, 1);
            fill_ep_ctx(input_ctx_phys, 2, ep_type_control, default_mps0(speed),
                        0, dev->ep0.phys, 8, 0);
            if (!run_command(trb_type_address_device, input_ctx_phys, 0,
                             slot, &code) ||
                code != code_success) {
                fail_step = 12;
                return false;
            }

            // Read the real bMaxPacketSize0; fix EP0 via Evaluate Context
            // when the speed-based guess was wrong. SuperSpeed encodes the
            // field as a power of two (9 -> 512) rather than a byte count.
            if (!get_descriptor(*dev, 1, 0, 8, &code)) {
                fail_step = 13;
                return false;
            }
            const hcpp::u16 real_mps =
                speed >= 4 ? static_cast<hcpp::u16>(1u << ctrl_buf[7])
                           : ctrl_buf[7];
            const hcpp::u16 guess_mps = default_mps0(speed);
            if (real_mps != 0 && real_mps != guess_mps) {
                zero_page(input_ctx_phys);
                ctl = ctx_dwords(input_ctx_phys, 0);
                ctl[1] = 1u << 1;  // evaluate EP0 ctx only
                fill_ep_ctx(input_ctx_phys, 2, ep_type_control, real_mps, 0,
                            dev->ep0.phys, 8, 0);
                if (!run_command(trb_type_evaluate_ctx, input_ctx_phys, 0,
                                 slot, &code) ||
                    code != code_success) {
                    fail_step = 14;
                    return false;
                }
            }

            // Full device descriptor, then the config descriptor (header,
            // then wTotalLength bytes capped at the scratch buffer size).
            if (!get_descriptor(*dev, 1, 0, 18, &code)) {
                fail_step = 15;
                return false;
            }
            dev->dev_class = ctrl_buf[4];
            if (!get_descriptor(*dev, 2, 0, 9, &code)) {
                fail_step = 16;
                return false;
            }
            hcpp::u16 total =
                static_cast<hcpp::u16>(ctrl_buf[2] | (hcpp::u16(ctrl_buf[3]) << 8));
            if (total > ctrl_buf_size) total = ctrl_buf_size;
            if (total < 9 || !get_descriptor(*dev, 2, 0, total, &code)) {
                fail_step = 17;
                return false;
            }
            const ClassScan scan = scan_config(ctrl_buf, total);

            // Hub class (device class 09 or a class-09 interface): the
            // device must be configured before its downstream ports power
            // up; usbhub.cc drives descriptor fetch, slot-context update
            // and per-port enumeration from here.
            if (dev->dev_class == 9 || scan.hub_found) {
                dev->kind = DevKind::Hub;
                if (scan.config_value == 0 ||
                    !set_configuration(*dev, scan.config_value)) {
                    fail_step = 18;
                    return false;
                }
                usbhub::hub_bind(dev, dev->speed, scan.hub_iface);
                return true;
            }

            // Mass-storage BOT interface (08/06/50): configure the bulk
            // OUT/IN pair, then hand the slot to usbmsc.cc which runs the
            // SCSI/BOT probe and registers into the block layer.
            if (scan.msc_found) {
                dev->kind = DevKind::Mass;
                dev->iface = scan.msc_iface;
                dev->bulk_out_dci =
                    static_cast<hcpp::u8>(scan.msc_out_ep * 2);
                dev->bulk_in_dci =
                    static_cast<hcpp::u8>(scan.msc_in_ep * 2 + 1);
                dev->bulk_out_ep = scan.msc_out_ep;
                dev->bulk_in_ep =
                    static_cast<hcpp::u8>(0x80 | scan.msc_in_ep);
                dev->bulk_out_mps = scan.msc_out_mps;
                dev->bulk_in_mps = scan.msc_in_mps;
                if (!ring_init(dev->bulk_out) || !ring_init(dev->bulk_in)) {
                    fail_step = 11;
                    return false;
                }
                if (!set_configuration(*dev, scan.config_value)) {
                    fail_step = 18;
                    return false;
                }
                const hcpp::u8 max_dci = dev->bulk_in_dci > dev->bulk_out_dci
                                             ? dev->bulk_in_dci
                                             : dev->bulk_out_dci;
                zero_page(input_ctx_phys);
                volatile hcpp::u32 *mctl = ctx_dwords(input_ctx_phys, 0);
                mctl[1] = (1u << 0) | (1u << dev->bulk_out_dci) |
                          (1u << dev->bulk_in_dci);
                fill_slot_ctx(input_ctx_phys, *dev, max_dci);
                fill_ep_ctx(input_ctx_phys, dev->bulk_out_dci + 1,
                            ep_type_bulk_out, dev->bulk_out_mps, 0,
                            dev->bulk_out.phys, 512, 0);
                fill_ep_ctx(input_ctx_phys, dev->bulk_in_dci + 1,
                            ep_type_bulk_in, dev->bulk_in_mps, 0,
                            dev->bulk_in.phys, 512, 0);
                if (!run_command(trb_type_configure_ep, input_ctx_phys, 0,
                                 slot, &code) ||
                    code != code_success) {
                    fail_step = 19;
                    return false;
                }
                usbmsc::msc_bind(dev, dev->iface);
                return true;
            }

            // USB Audio Class: AC + AS with an isoch-OUT endpoint.
            // usbuac.cc parses the class-specific descriptors itself
            // from the config blob still sitting in ctrl_buf, picks a
            // PCM alt setting, arms the isoch pipe via host_iso_arm()
            // and streams a burst of 1 ms TDs.
            if (scan.uac_found) {
                dev->kind = DevKind::Audio;
                dev->iface = scan.uac_ac_iface;
                if (!set_configuration(*dev, scan.config_value)) {
                    fail_step = 18;
                    return false;
                }
                usbuac::uac_bind(dev, ctrl_buf, total);
                return true;
            }

            // Boot-protocol HID interface (class 03, protocol 1 kbd /
            // 2 mouse): usbhid.cc issues the SET_PROTOCOL/SET_IDLE class
            // requests and arms the interrupt-IN endpoint through
            // host_intr_arm(); report completions land in
            // handle_transfer_event above and are decoded by
            // usbhid::hid_report().
            if (!scan.hid_found || scan.ep_num == 0 || scan.ep_mps == 0) {
                dev->kind = DevKind::Other;
                return true;  // unsupported class: slot claimed, nothing armed
            }
            dev->iface = scan.iface;
            if (!set_configuration(*dev, scan.config_value)) {
                fail_step = 18;
                return false;
            }
            if (!usbhid::hid_bind(dev, scan.iface, scan.protocol,
                                  scan.ep_num, scan.ep_mps,
                                  scan.ep_interval)) {
                fail_step = 19;
                return false;
            }
            dev->kind =
                scan.protocol == 1 ? DevKind::Keyboard : DevKind::Mouse;
            return true;
        }

        // -----------------------------------------------------------------
        // Controller bring-up
        // -----------------------------------------------------------------

        bool controller_init(const pci::PciDevice &pdev) noexcept {
            const hcpp::u32 bar0 = pci::bar(pdev, 0);
            const hcpp::u32 bar1 = pci::bar(pdev, 1);
            if (!pci::bar_is_memory(bar0)) {
                fail_step = 2;
                return false;
            }
            const hcpp::u64 base = pci::bar_memory_address(bar0, bar1);
            if (base == 0 || base >= 0x100000000ull) {
                fail_step = 3;
                return false;
            }
            pci::pci_enable_mem(pdev);
            pci::pci_enable_busmaster(pdev);
            mmio = static_cast<hcpp::uintptr_t>(base);
            // The kernel identity-maps MMIO low enough for the other PCI
            // drivers; verify rather than assume, like the HDA path does,
            // and identity-map the first 32 KiB of the BAR when it is not.
            if (memory::vmm_initialized() && memory::vmm_translate(mmio) != mmio) {
                bool mapped = true;
                for (hcpp::u32 i = 0; i < 8 && mapped; ++i) {
                    const hcpp::uintptr_t page = mmio + i * memory::page_size;
                    if (memory::vmm_translate(page) != page &&
                        !memory::vmm_map_page(page, page, true, false)) {
                        mapped = false;
                    }
                }
                if (!mapped) {
                    fail_step = 4;
                    return false;
                }
            }

            const hcpp::u32 caplen =
                cap32(cap_caplength) & 0xFF;  // CAPLENGTH is a byte at 0x00
            op_base = mmio + caplen;
            rt_base = mmio + (cap32(cap_rtsoff) & ~0x1Fu);
            db_base = mmio + (cap32(cap_dboff) & ~0x3u);
            const hcpp::u32 hcs1 = cap32(cap_hcsparams1);
            hc_max_slots = hcs1 & 0xFF;
            hc_max_ports = (hcs1 >> 24) & 0xFF;
            if (hc_max_ports > max_ports) hc_max_ports = max_ports;
            ctx_size = (cap32(cap_hccparams1) & 4u) != 0 ? 64 : 32;
            if (hc_max_slots == 0 || hc_max_ports == 0) {
                fail_step = 5;
                return false;
            }
            slots_enabled = hc_max_slots < max_slots ? hc_max_slots : max_slots;

            // Halt, then reset; all waits bounded by PIT ticks.
            op32(op_usbcmd) = op32(op_usbcmd) & ~usbcmd_rs;
            if (!wait_flag(op32(op_usbsts), usbsts_hchalted, true,
                           wait_halt_ticks)) {
                fail_step = 6;
                return false;
            }
            op32(op_usbcmd) = op32(op_usbcmd) | usbcmd_hcrst;
            if (!wait_flag(op32(op_usbcmd), usbcmd_hcrst, false,
                           wait_reset_ticks) ||
                !wait_flag(op32(op_usbsts), usbsts_cnr, false,
                           wait_reset_ticks)) {
                fail_step = 7;
                return false;
            }

            dcbaa_phys = alloc_page();
            input_ctx_phys = alloc_page();
            ctrl_buf_phys = alloc_page();
            if (dcbaa_phys == 0 || input_ctx_phys == 0 || ctrl_buf_phys == 0 ||
                !ring_init(cmd_ring)) {
                fail_step = 8;
                return false;
            }
            dcbaa = reinterpret_cast<volatile hcpp::u64 *>(dcbaa_phys);
            ctrl_buf = reinterpret_cast<volatile hcpp::u8 *>(ctrl_buf_phys);
            erst_phys = alloc_page();
            evt_ring_phys = alloc_page();
            if (erst_phys == 0 || evt_ring_phys == 0) {
                fail_step = 8;
                return false;
            }
            erst = reinterpret_cast<volatile ErstEntry *>(erst_phys);
            evt_trbs = reinterpret_cast<volatile Trb *>(evt_ring_phys);
            erst[0].address = evt_ring_phys;
            erst[0].size = static_cast<hcpp::u32>(ring_trbs);
            erst[0].reserved = 0;
            evt_dequeue = 0;
            evt_ccs = true;

            op_write64(op_dcbaap, dcbaa_phys);
            op_write64(op_crcr, cmd_ring.phys | 1u);  // RCS = producer cycle 1
            op32(op_config) = slots_enabled;

            // Interrupter 0: single-segment event ring, interrupts masked
            // off (IMAN.IE = 0) since we poll ERDP directly.
            intr32(intr_iman) = 1;  // clear pending IP bit (RW1C), IE stays 0
            intr32(intr_imod) = 0;
            intr32(intr_erstsz) = 1;
            intr_write64(intr_erstba, erst_phys);
            intr_write64(intr_erdp, evt_ring_phys | erdp_ehb);

            op32(op_usbcmd) = op32(op_usbcmd) | usbcmd_rs | usbcmd_inte;
            if (!wait_flag(op32(op_usbsts), usbsts_hchalted, false,
                           wait_halt_ticks)) {
                fail_step = 9;
                return false;
            }
            return true;
        }

    }  // namespace

    // -----------------------------------------------------------------
    // xhci_host.h API: narrow transfer/enumeration hooks for usbhub.cc
    // and usbmsc.cc. All calls run during the serialized enumeration
    // window (usb_initialize) or afterwards from single-threaded block
    // I/O; nothing here is re-entrant.
    // -----------------------------------------------------------------

    bool host_control(HostDevice *dev, hcpp::u8 bm_request_type,
                      hcpp::u8 b_request, hcpp::u16 w_value,
                      hcpp::u16 w_index, hcpp::u16 w_length,
                      hcpp::u64 data_phys, bool dir_in) noexcept {
        if (dev == nullptr) return false;
        return control_transfer(*dev, bm_request_type, b_request, w_value,
                                w_index, w_length, data_phys, dir_in);
    }

    hcpp::u8 host_bulk(HostDevice *dev, bool dir_in, hcpp::u64 phys,
                       hcpp::u32 length, hcpp::u32 *residual) noexcept {
        if (dev == nullptr) return code_wait_timeout;
        Ring &ring = dir_in ? dev->bulk_in : dev->bulk_out;
        const hcpp::u8 dci = dir_in ? dev->bulk_in_dci : dev->bulk_out_dci;
        const hcpp::u64 trb_phys =
            ring_push(ring, phys, length,
                      (hcpp::u32(trb_type_normal) << 10) | trb_ioc);
        ring_doorbell(dev->slot_id, dci);
        Trb ev{};
        if (!wait_event(trb_phys, false, wait_transfer_ticks, ev)) {
            return code_wait_timeout;
        }
        if (residual != nullptr) *residual = ev.status & 0xFFFFFFu;
        return trb_code(ev);
    }

    bool host_clear_halt(HostDevice *dev, bool dir_in) noexcept {
        if (dev == nullptr) return false;
        Ring &ring = dir_in ? dev->bulk_in : dev->bulk_out;
        const hcpp::u8 dci = dir_in ? dev->bulk_in_dci : dev->bulk_out_dci;
        const hcpp::u8 ep_addr =
            dir_in ? dev->bulk_in_ep : dev->bulk_out_ep;
        hcpp::u8 code = 0;
        // Host side first: unhalt the endpoint context, then move its
        // dequeue pointer to the ring's current producer position (the
        // stalled TRB is skipped entirely).
        if (!run_command(trb_type_reset_endpoint, 0,
                         hcpp::u32(dci) << 16, dev->slot_id, &code) ||
            code != code_success) {
            return false;
        }
        const hcpp::u64 dequeue =
            (ring.phys + ring.enqueue * sizeof(Trb)) | (ring.pcs ? 1u : 0u);
        if (!run_command(trb_type_set_tr_dequeue, dequeue,
                         hcpp::u32(dci) << 16, dev->slot_id, &code) ||
            code != code_success) {
            return false;
        }
        // Device side: CLEAR_FEATURE(ENDPOINT_HALT) on the endpoint.
        return control_transfer(*dev, 0x02, 1, 0, ep_addr, 0, 0, false);
    }

    bool host_set_hub_context(HostDevice *dev, hcpp::u8 num_ports,
                              hcpp::u8 ttt) noexcept {
        if (dev == nullptr) return false;
        dev->is_hub = true;
        dev->hub_ports = num_ports;
        dev->ttt = ttt;
        zero_page(input_ctx_phys);
        volatile hcpp::u32 *ctl = ctx_dwords(input_ctx_phys, 0);
        ctl[1] = 1u << 0;  // evaluate slot context only
        fill_slot_ctx(input_ctx_phys, *dev, 1);
        hcpp::u8 code = 0;
        return run_command(trb_type_evaluate_ctx, input_ctx_phys, 0,
                           dev->slot_id, &code) &&
               code == code_success;
    }

    void host_enumerate_child(HostDevice *dev, hcpp::u8 port,
                              hcpp::u8 speed) noexcept {
        if (dev == nullptr || port == 0 || speed == 0) return;
        if (dev->depth >= 5) return;  // route string holds 5 nibbles
        const hcpp::u32 route =
            ((dev->route << 4) | (hcpp::u32(port) & 0xF)) & 0xFFFFFu;
        // TT fields matter only for LS/FS devices behind a high-speed hub.
        const bool need_tt = dev->speed == 3 && speed < 3;
        enumerate_device(port, speed, dev->root_port, route,
                         need_tt ? dev->slot_id : 0,
                         need_tt ? port : 0,
                         static_cast<hcpp::u8>(dev->depth + 1));
    }

    bool host_intr_arm(HostDevice *dev, hcpp::u8 ep_num, hcpp::u16 mps,
                       hcpp::u8 binterval) noexcept {
        if (dev == nullptr || ep_num == 0 || ep_num > 15 || mps == 0) {
            return false;
        }
        if (!ring_init(dev->intr)) return false;
        dev->ep_dci = static_cast<hcpp::u8>(ep_num * 2 + 1);  // IN ep
        dev->ep_mps = mps;
        dev->ep_interval = binterval;
        dev->report_len = mps > 64 ? 64 : mps;
        // Configure Endpoint: input ctx adds A0 (slot, with updated
        // context-entries count) + A<dci> for the interrupt-IN ep.
        zero_page(input_ctx_phys);
        volatile hcpp::u32 *ctl = ctx_dwords(input_ctx_phys, 0);
        ctl[1] = (1u << 0) | (1u << dev->ep_dci);
        fill_slot_ctx(input_ctx_phys, *dev, dev->ep_dci);
        fill_ep_ctx(input_ctx_phys, dev->ep_dci + 1, ep_type_interrupt_in,
                    dev->ep_mps, ep_interval_encoding(dev->ep_interval),
                    dev->intr.phys, dev->report_len, dev->ep_mps);
        hcpp::u8 code = 0;
        if (!run_command(trb_type_configure_ep, input_ctx_phys, 0,
                         dev->slot_id, &code) ||
            code != code_success) {
            dev->ep_dci = 0;
            return false;
        }
        post_intr_trb(*dev);  // arm the periodic report poll
        return true;
    }

    bool host_iso_arm(HostDevice *dev, hcpp::u8 ep_num, hcpp::u16 mps,
                      hcpp::u8 binterval) noexcept {
        if (dev == nullptr || ep_num == 0 || ep_num > 15 || mps == 0 ||
            dev->iso_dci != 0) {
            return false;
        }
        if (!ring_init(dev->iso)) return false;
        dev->iso_dci = static_cast<hcpp::u8>(ep_num * 2);  // OUT ep DCI
        dev->iso_mps = mps;
        // xHCI EP-context Interval for isoch endpoints is bInterval - 1
        // (unlike the log2 encoding interrupt endpoints use).
        const hcpp::u8 interval =
            static_cast<hcpp::u8>(binterval == 0 ? 0 : binterval - 1);
        zero_page(input_ctx_phys);
        volatile hcpp::u32 *ctl = ctx_dwords(input_ctx_phys, 0);
        ctl[1] = (1u << 0) | (1u << dev->iso_dci);
        fill_slot_ctx(input_ctx_phys, *dev, dev->iso_dci);
        fill_ep_ctx(input_ctx_phys, dev->iso_dci + 1, ep_type_isoch_out,
                    mps, interval, dev->iso.phys, mps, mps);
        hcpp::u8 code = 0;
        if (!run_command(trb_type_configure_ep, input_ctx_phys, 0,
                         dev->slot_id, &code) ||
            code != code_success) {
            dev->iso_dci = 0;
            return false;
        }
        return true;
    }

    bool host_iso_queue(HostDevice *dev, hcpp::u64 phys,
                        hcpp::u32 len) noexcept {
        if (dev == nullptr || dev->iso_dci == 0 || len == 0 ||
            len > 0x1FFFFu) {
            return false;
        }
        ring_push(dev->iso, phys, len,
                  (hcpp::u32(trb_type_isoch) << 10) | trb_ioc | trb_sia);
        return true;
    }

    void host_iso_kick(HostDevice *dev) noexcept {
        if (dev == nullptr || dev->iso_dci == 0) return;
        ring_doorbell(dev->slot_id, dev->iso_dci);
    }

    hcpp::u64 host_scratch() noexcept { return ctrl_buf_phys; }

    bool usb_initialize() noexcept {
        present = false;
        running = false;
        fail_step = 0;
        usbhid::hid_reset();
        have_cmd_event = have_xfer_event = false;
        for (auto &d : devices) d = Device{};

        const pci::PciDevice *pdev = pci::find_class(0x0C, 0x03);
        if (pdev == nullptr || pdev->program_interface != 0x30) {
            serial::write_line("xhci dev=absent");
            serial::write_line("uac dev=absent");
            return true;  // graceful: PS/2 input path keeps working
        }
        if (!controller_init(*pdev)) {
            serial::write("xhci dev=fail s=");
            serial::write_dec32(fail_step);
            serial::write("\r\n");
            serial::write_line("uac dev=absent");
            return true;  // never fail the boot over USB
        }
        present = true;
        running = true;
        serial::write("xhci dev=ok slots=");
        serial::write_dec32(slots_enabled);
        serial::write("\r\n");

        // Root ports first; any hub found during enumerate_device()
        // recursively walks its downstream ports via host_enumerate_child().
        // port_reset() powers up each port, waits for connect status,
        // resets it when needed and bails out quickly for empty ports.
        for (hcpp::u32 port = 1; port <= hc_max_ports; ++port) {
            if (!port_reset(port)) continue;
            const hcpp::u8 speed =
                static_cast<hcpp::u8>((portsc(port) >> 10) & 0x0F);
            enumerate_device(static_cast<hcpp::u8>(port), speed,
                             static_cast<hcpp::u8>(port), 0, 0, 0, 0);
        }
        if (!usbuac::uac_device_seen()) {
            serial::write_line("uac dev=absent");
        }
        return true;
    }

    bool xhci_present() noexcept { return present; }

    hcpp::u32 usb_device_count() noexcept {
        hcpp::u32 n = 0;
        for (const auto &d : devices)
            if (d.used) ++n;
        return n;
    }

    bool usb_keyboard_present() noexcept {
        return usbhid::hid_keyboard_present();
    }

    bool usb_mouse_present() noexcept {
        return usbhid::hid_mouse_present();
    }

    void usb_poll() noexcept {
        if (!running) return;
        Trb t{};
        while (evt_pop(t)) dispatch_event(t);
    }

    hcpp::u32 usb_mouse_poll(input::MsauxPacket *out, hcpp::u32 max) noexcept {
        return usbhid::hid_mouse_poll(out, max);
    }

}  // namespace hype::usb
