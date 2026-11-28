// OHCI (1394 Open Host Controller Interface) bus driver + node
// enumeration + AMDTP bring-up for TASCAM-class audio units.
//
// Implemented for real: PCI probe of class 0x0C/0x00/0x10, BAR0 MMIO,
// soft-reset, LPS/link enable, self-ID DMA + parse, IEEE 1212 config-ROM
// walk via the async transmit (ATRQ) / async response receive (ARRS)
// DMA contexts, IRM channel allocation by compare-swap lock transaction,
// IR iso context in packet-per-buffer mode delivering AMDTP payloads to
// the tascam record ring. All waits are polled (PCI INTx is not routed
// in this kernel), bounded by pit_ticks(). With no OHCI device on the
// PCI bus the probe is a graceful "fw dev=absent" and returns.

#include "kernel/firewire/firewirebus.h"

#include "hcpp/memory.hpp"
#include "kernel/firewire/amdtpam824.h"
#include "kernel/firewire/amdtpstream.h"
#include "kernel/firewire/amdtpstreampipe.h"
#include "kernel/firewire/iso-resources.h"
#include "kernel/firewire/tascam_devkey.h"
#include "kernel/firewire/tascam_record.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace hype::fw {
    namespace {

        // -----------------------------------------------------------
        // OHCI register map (OHCI 1.1 §5 / Linux ohci.h)
        // -----------------------------------------------------------

        constexpr hcpp::u32 reg_at_retries = 0x008;
        constexpr hcpp::u32 reg_hccontrol_set = 0x050;
        [[maybe_unused]] constexpr hcpp::u32 reg_hccontrol_clear = 0x054;
        constexpr hcpp::u32 reg_self_id_buffer = 0x064;
        constexpr hcpp::u32 reg_self_id_count = 0x068;
        constexpr hcpp::u32 reg_int_event_set = 0x080;
        constexpr hcpp::u32 reg_int_event_clear = 0x084;
        constexpr hcpp::u32 reg_int_mask_set = 0x088;
        constexpr hcpp::u32 reg_iso_xmit_int_mask_set = 0x098;
        constexpr hcpp::u32 reg_iso_recv_int_mask_set = 0x0A8;
        constexpr hcpp::u32 reg_link_control_set = 0x0E0;
        constexpr hcpp::u32 reg_node_id = 0x0E8;
        constexpr hcpp::u32 reg_as_req_filter_hi_set = 0x100;
        constexpr hcpp::u32 reg_as_req_filter_lo_set = 0x108;
        constexpr hcpp::u32 reg_phy_req_filter_hi_set = 0x110;
        constexpr hcpp::u32 reg_phy_req_filter_lo_set = 0x118;
        constexpr hcpp::u32 reg_phy_upper_bound = 0x120;
        constexpr hcpp::u32 reg_atrq_ctrl_set = 0x180;
        [[maybe_unused]] constexpr hcpp::u32 reg_atrq_ctrl_clear = 0x184;
        constexpr hcpp::u32 reg_atrq_cmd_ptr = 0x18C;
        constexpr hcpp::u32 reg_arrq_ctrl_set = 0x1C0;
        constexpr hcpp::u32 reg_arrq_ctrl_clear = 0x1C4;
        constexpr hcpp::u32 reg_arrq_cmd_ptr = 0x1CC;
        constexpr hcpp::u32 reg_arrs_ctrl_set = 0x1E0;
        constexpr hcpp::u32 reg_arrs_ctrl_clear = 0x1E4;
        constexpr hcpp::u32 reg_arrs_cmd_ptr = 0x1EC;

        constexpr hcpp::u32 reg_ir_ctrl_set(hcpp::u32 n) { return 0x400 + 32 * n; }
        constexpr hcpp::u32 reg_ir_ctrl_clear(hcpp::u32 n) { return 0x404 + 32 * n; }
        constexpr hcpp::u32 reg_ir_cmd_ptr(hcpp::u32 n) { return 0x40C + 32 * n; }
        constexpr hcpp::u32 reg_ir_context_match(hcpp::u32 n) { return 0x410 + 32 * n; }
        [[maybe_unused]] constexpr hcpp::u32 reg_it_ctrl_set(hcpp::u32 n) { return 0x200 + 16 * n; }
        [[maybe_unused]] constexpr hcpp::u32 reg_it_ctrl_clear(hcpp::u32 n) { return 0x204 + 16 * n; }
        [[maybe_unused]] constexpr hcpp::u32 reg_it_cmd_ptr(hcpp::u32 n) { return 0x20C + 16 * n; }

        constexpr hcpp::u32 hccontrol_soft_reset = 0x00010000u;
        constexpr hcpp::u32 hccontrol_link_enable = 0x00020000u;
        constexpr hcpp::u32 hccontrol_posted_write = 0x00040000u;
        constexpr hcpp::u32 hccontrol_lps = 0x00080000u;
        constexpr hcpp::u32 hccontrol_phy_enable = 0x00800000u;

        constexpr hcpp::u32 link_rcv_self_id = 1u << 9;
        constexpr hcpp::u32 link_rcv_phy_pkt = 1u << 10;
        constexpr hcpp::u32 link_cycle_timer_enable = 1u << 20;

        constexpr hcpp::u32 node_id_valid = 0x80000000u;
        constexpr hcpp::u32 node_id_root = 0x40000000u;

        constexpr hcpp::u32 int_rsp_pkt = 1u << 5;
        constexpr hcpp::u32 int_isoch_rx = 1u << 7;
        constexpr hcpp::u32 int_self_id_complete = 1u << 16;
        constexpr hcpp::u32 int_bus_reset = 1u << 17;
        constexpr hcpp::u32 int_master_enable = 0x80000000u;

        constexpr hcpp::u32 selfid_count_error = 0x80000000u;

        // DMA descriptor (16 bytes, 16-byte aligned), host-endian.
        struct OhciDesc {
            hcpp::u16 req_count;
            hcpp::u16 control;
            hcpp::u32 data_address;
            hcpp::u32 branch_address;
            hcpp::u16 res_count;
            hcpp::u16 transfer_status;
        };
        static_assert(sizeof(OhciDesc) == 16);

        [[maybe_unused]] constexpr hcpp::u16 desc_output_more = 0;
        constexpr hcpp::u16 desc_output_last = 1u << 12;
        constexpr hcpp::u16 desc_input_more = 2u << 12;
        constexpr hcpp::u16 desc_input_last = 3u << 12;
        [[maybe_unused]] constexpr hcpp::u16 desc_status = 1u << 11;
        constexpr hcpp::u16 desc_key_immediate = 2u << 8;
        constexpr hcpp::u16 desc_irq_always = 3u << 4;
        constexpr hcpp::u16 desc_branch_always = 3u << 2;

        constexpr hcpp::u32 ctx_run = 0x8000;
        constexpr hcpp::u32 ctx_wake = 0x1000;
        [[maybe_unused]] constexpr hcpp::u32 ctx_dead = 0x0800;
        constexpr hcpp::u32 ctx_active = 0x0400;

        constexpr hcpp::u32 ir_isoch_header = 0x40000000u;
        [[maybe_unused]] constexpr hcpp::u32 it_cycle_match_enable = 0x80000000u;

        // -----------------------------------------------------------
        // Driver state
        // -----------------------------------------------------------

        volatile hcpp::u8 *mmio;
        bool present;
        [[maybe_unused]] hcpp::u8 fail_step;

        hcpp::uintptr_t selfid_phys;
        volatile hcpp::u32 *selfid_buf;   // one page; [0]=generation/tstamp

        // ATRQ program page: descriptor slots for one request program.
        hcpp::uintptr_t at_page_phys;
        volatile OhciDesc *at_desc;

        // ARRQ/ARRS receive rings: 8 buffers x 512 B each, descriptor
        // ring in the second half of the descriptor page.
        constexpr hcpp::u32 ar_buffers = 8;
        constexpr hcpp::u32 ar_buf_bytes = 512;
        hcpp::uintptr_t ar_buf_phys;
        volatile hcpp::u8 *ar_buf;
        hcpp::uintptr_t ar_desc_phys;
        volatile OhciDesc *ar_desc;
        hcpp::u32 arrs_head;   // next ARRS buffer index to inspect

        // IR ring: 16 packet-per-buffer slots of 2048 B = 8 pages.
        constexpr hcpp::u32 ir_buffers = 16;
        constexpr hcpp::u32 ir_buf_bytes = 2048;
        hcpp::uintptr_t ir_buf_phys;
        volatile hcpp::u8 *ir_buf;
        hcpp::uintptr_t ir_desc_phys;
        volatile OhciDesc *ir_desc;
        hcpp::u32 ir_harvest;      // next buffer to inspect
        hcpp::u32 ir_packet_count;
        hcpp::u8 ir_ctx_index;
        FwIsoRxFn rx_deliver;
        void *rx_deliver_ctx;

        // Pending async transaction state (single outstanding txn).
        hcpp::u8 txn_tlabel;

        FwNode nodes[fw_max_nodes];
        hcpp::u32 node_count;
        hcpp::u16 local_node_id;   // full 16-bit (bus << 6) | node
        hcpp::u8 irm_node;
        hcpp::u32 generation;
        hcpp::u32 bus_resets;
        hcpp::u64 channels_in_use;
        hcpp::u32 bandwidth_claimed;

        // Static DMA scratch for block writes/reads (BSS is identity
        // mapped below 4 GiB, so it is bus-master safe).
        alignas(16) hcpp::u8 txn_payload[512];

        // -----------------------------------------------------------
        // Small helpers
        // -----------------------------------------------------------

        hcpp::u32 mmio_read(hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(mmio + off);
        }
        void mmio_write(hcpp::u32 off, hcpp::u32 value) noexcept {
            *reinterpret_cast<volatile hcpp::u32 *>(mmio + off) = value;
        }
        void barrier() noexcept { asm volatile("" ::: "memory"); }

        constexpr hcpp::u32 bswap32(hcpp::u32 v) noexcept {
            return (v >> 24) | ((v >> 8) & 0xFF00u) |
                   ((v << 8) & 0xFF0000u) | (v << 24);
        }

        bool wait_flag(volatile hcpp::u32 &reg, hcpp::u32 mask, bool set,
                       hcpp::u64 ticks) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            while (((reg & mask) != 0) != set) {
                if (interrupts::pit_ticks() - start > ticks) return false;
                asm volatile("pause");
            }
            return true;
        }

        volatile hcpp::u32 &reg_ref(hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(
                const_cast<hcpp::u8 *>(mmio) + off);
        }

        // -----------------------------------------------------------
        // Async DMA contexts
        // -----------------------------------------------------------

        // One AR context: ar_desc[i] describes ar_buf[i*512], branch to
        // i+1 mod 8 so the ring wraps. `base_off` selects ARRQ or ARRS.
        bool ar_context_start(hcpp::u32 set_off, hcpp::u32 clear_off,
                              hcpp::u32 ptr_off, hcpp::u32 desc_base,
                              hcpp::u32 buf_base) noexcept {
            volatile OhciDesc *d = ar_desc + desc_base;
            for (hcpp::u32 i = 0; i < ar_buffers; ++i) {
                const hcpp::u32 phys = static_cast<hcpp::u32>(ar_buf_phys) +
                                       buf_base + i * ar_buf_bytes;
                volatile OhciDesc *next =
                    (i + 1 < ar_buffers) ? &d[i + 1] : &d[0];
                const hcpp::u32 next_phys =
                    static_cast<hcpp::u32>(ar_desc_phys) +
                    static_cast<hcpp::u32>(
                        (next - ar_desc) * sizeof(OhciDesc));
                d[i].req_count = static_cast<hcpp::u16>(ar_buf_bytes);
                d[i].control = desc_input_more | desc_branch_always;
                d[i].data_address = phys;
                d[i].branch_address = next_phys | 1;
                d[i].res_count = 0;
                d[i].transfer_status = 0;
            }
            mmio_write(clear_off, ~0u);
            if (!wait_flag(reg_ref(set_off), ctx_active, false, 100)) {
                return false;
            }
            const hcpp::u32 head_phys =
                static_cast<hcpp::u32>(ar_desc_phys) +
                desc_base * sizeof(OhciDesc);
            mmio_write(ptr_off, head_phys | 1);
            barrier();
            mmio_write(set_off, ctx_run);
            return true;
        }

        // Queue one async request on the ATRQ context. `header` is the
        // 3-4 quadlet OHCI AT packet image (host-endian u32s squashed
        // into the immediate data after descriptor 0). `payload` is the
        // optional block data, DMA'd from its own descriptor.
        bool atrq_send(const hcpp::u32 header[4], hcpp::u32 header_bytes,
                       const void *payload, hcpp::u32 payload_bytes) noexcept {
            volatile hcpp::u32 *imm =
                reinterpret_cast<volatile hcpp::u32 *>(
                    const_cast<volatile OhciDesc *>(&at_desc[1]));
            for (hcpp::u32 i = 0; i < 4; ++i) imm[i] = header[i];

            hcpp::u32 z;
            at_desc[0].req_count = static_cast<hcpp::u16>(header_bytes);
            at_desc[0].data_address = 0;
            at_desc[0].branch_address = 0;
            at_desc[0].res_count = 0;
            at_desc[0].transfer_status = 0;
            at_desc[0].control = desc_key_immediate;
            if (payload_bytes != 0) {
                hcpp::copy(txn_payload, payload, payload_bytes);
                at_desc[2].req_count = static_cast<hcpp::u16>(payload_bytes);
                at_desc[2].control = desc_output_last | desc_irq_always |
                                     desc_branch_always;
                at_desc[2].data_address =
                    static_cast<hcpp::u32>(
                        reinterpret_cast<hcpp::uintptr_t>(txn_payload));
                at_desc[2].branch_address = 0;
                at_desc[2].res_count = 0;
                at_desc[2].transfer_status = 0;
                z = 3;
            } else {
                at_desc[0].control |= desc_output_last | desc_irq_always |
                                      desc_branch_always;
                z = 2;
            }
            barrier();

            const bool running = (mmio_read(reg_atrq_ctrl_set) & ctx_run) != 0;
            mmio_write(reg_atrq_cmd_ptr,
                       static_cast<hcpp::u32>(at_page_phys) | z);
            barrier();
            if (running) {
                mmio_write(reg_atrq_ctrl_set, ctx_wake);
            } else {
                mmio_write(reg_atrq_ctrl_set, ctx_run);
            }
            return true;
        }

        // Poll the ARRS ring for a completed response buffer. Returns
        // the buffer index or -1. The HW writes res_count = bytes left
        // over when a packet completes; a nonzero res_count marks a
        // delivered packet.
        int arrs_scan() noexcept {
            for (hcpp::u32 n = 0; n < ar_buffers; ++n) {
                const hcpp::u32 i = (arrs_head + n) % ar_buffers;
                volatile OhciDesc &d = ar_desc[ar_buffers + i];
                if (d.res_count != 0 || d.transfer_status != 0) {
                    arrs_head = (i + 1) % ar_buffers;
                    // Re-arm the slot: full request again, status clear.
                    const hcpp::u32 used =
                        ar_buf_bytes - d.res_count;
                    d.res_count = 0;
                    d.transfer_status = 0;
                    return static_cast<int>(i) | static_cast<int>(used << 8);
                }
            }
            return -1;
        }

        // Wait for the response matching `tlabel`; copies the packet
        // quadlets (IEEE 1394 wire image) into `out` (up to out_quads).
        // Returns rcode, or rcode_no_ack on timeout/mismatch.
        hcpp::u8 await_response(hcpp::u8 tlabel, hcpp::u8 want_tcode,
                                hcpp::u32 *out, hcpp::u32 out_quads,
                                hcpp::u32 *out_bytes) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            for (;;) {
                int slot = arrs_scan();
                if (slot >= 0) {
                    const hcpp::u32 idx = static_cast<hcpp::u32>(slot) & 0xFF;
                    const hcpp::u32 used = static_cast<hcpp::u32>(slot) >> 8;
                    volatile hcpp::u8 *buf =
                        ar_buf + memory::page_size + idx * ar_buf_bytes;
                    hcpp::u32 q0 = *reinterpret_cast<volatile hcpp::u32 *>(buf);
                    const hcpp::u8 rl = static_cast<hcpp::u8>((q0 >> 10) & 0x3F);
                    const hcpp::u8 tc = static_cast<hcpp::u8>((q0 >> 4) & 0xF);
                    if (rl == tlabel && tc == want_tcode) {
                        const hcpp::u32 quads =
                            used / 4 < out_quads ? used / 4 : out_quads;
                        for (hcpp::u32 i = 0; i < quads; ++i) {
                            out[i] = *reinterpret_cast<volatile hcpp::u32 *>(
                                buf + i * 4);
                        }
                        if (out_bytes != nullptr) *out_bytes = used;
                        hcpp::set(const_cast<hcpp::u8 *>(
                                      static_cast<const volatile hcpp::u8 *>(
                                          buf)),
                                  0, ar_buf_bytes);
                        return static_cast<hcpp::u8>((out[1] >> 12) & 0xF);
                    }
                }
                if (interrupts::pit_ticks() - start > 50) {
                    return rcode_no_ack;
                }
                asm volatile("pause");
            }
        }

        // Build + queue a request and wait for its response.
        //   tcode: read/write/lock request tcode
        //   q3:    quadlet-3 value (dataLength<<16|extTcode for block /
        //          lock, bswap32'd data for write-quadlet, unused for
        //          read-quadlet which sends a 12-byte header)
        hcpp::u8 transact(hcpp::u8 node, hcpp::u8 tcode, hcpp::u64 offset,
                          const void *payload, hcpp::u32 payload_bytes,
                          hcpp::u32 q3, hcpp::u32 *resp,
                          hcpp::u32 resp_quads, hcpp::u32 *resp_bytes) noexcept {
            if (!present) return rcode_no_ack;
            const hcpp::u8 tlabel = txn_tlabel;
            txn_tlabel = (txn_tlabel + 1) & 0x3F;
            const hcpp::u16 dest_id = static_cast<hcpp::u16>(
                (local_node_id & 0xFFC0u) | (node & 0x3Fu));

            hcpp::u32 header[4];
            header[0] = (hcpp::u32(speed_s400) << 16) |
                        (hcpp::u32(tlabel) << 10) |
                        (hcpp::u32(tcode) << 4);
            header[1] = (hcpp::u32(dest_id) << 16) |
                        static_cast<hcpp::u32>((offset >> 32) & 0xFFFFu);
            header[2] = static_cast<hcpp::u32>(offset);
            header[3] = q3;

            // Header lengths: quadlet read = 3 quads, write-quadlet /
            // block / lock = 4 quads. A block *payload* travels on the
            // third descriptor.
            const hcpp::u32 header_bytes =
                tcode == tcode_read_quadlet_request ? 12 : 16;
            atrq_send(header, header_bytes, payload, payload_bytes);

            const hcpp::u8 want = tcode == tcode_read_quadlet_request
                                      ? tcode_read_quadlet_response
                                  : tcode == tcode_read_block_request
                                      ? tcode_read_block_response
                                  : tcode == tcode_lock_request
                                      ? tcode_lock_response
                                      : tcode_write_response;
            return await_response(tlabel, want, resp, resp_quads, resp_bytes);
        }

        // -----------------------------------------------------------
        // Self-ID parsing + node enumeration
        // -----------------------------------------------------------

        // Self-ID quadlet 0 fields (IEEE 1394a §4.3.4.1, Linux
        // core-topology.c masks):
        //   [29:24] phy id, [23] extended, [22] link-on, [21:16] gap cnt,
        //   [15:14] phy speed, [11] contender, [7:2] ports p0..p2,
        //   [1] initiated reset, [0] more packets.
        void parse_self_id() noexcept {
            const hcpp::u32 count_reg = mmio_read(reg_self_id_count);
            if ((count_reg & selfid_count_error) != 0) return;
            generation = (count_reg >> 16) & 0xFF;
            const hcpp::u32 quadlets = (count_reg >> 2) & 0x1FF;
            node_count = 0;
            for (hcpp::u32 i = 1; i + 1 <= quadlets && i < 250;) {
                const hcpp::u32 q = selfid_buf[i];
                if (((q >> 30) & 0x3u) != 2u || ((q >> 23) & 1u) != 0) {
                    ++i;   // not a base self-ID packet
                    continue;
                }
                const hcpp::u8 phy_id = static_cast<hcpp::u8>((q >> 24) & 0x3F);
                if (phy_id >= fw_max_nodes) {
                    ++i;
                    continue;
                }
                FwNode &n = nodes[node_count];
                n = {};
                n.node_id = phy_id;
                n.self_id_seen = true;
                n.link_active = (q >> 22 & 1u) != 0;
                n.speed = static_cast<hcpp::u8>((q >> 14) & 0x3);
                n.port_status[0] = static_cast<hcpp::u8>((q >> 6) & 0x3);
                n.port_status[1] = static_cast<hcpp::u8>((q >> 4) & 0x3);
                n.port_status[2] = static_cast<hcpp::u8>((q >> 2) & 0x3);
                n.port_status[3] = 0;
                ++node_count;
                i += 1 + ((q & 1u) ? 1u : 0u);  // skip extended quads
            }
        }

        // Read one config-ROM quadlet. Returns the raw value or false.
        bool rom_quadlet(hcpp::u8 node, hcpp::u32 index,
                         hcpp::u32 *value) noexcept {
            return fw_read_quadlet(node, csr_config_rom + index * 4, value);
        }

        // IEEE 1212 general ROM walk: bus-info block for guid/irmc, then
        // root-directory immediates for Vendor_ID (0x03) / Model_ID
        // (0x17). Bounded to 40 quadlets so a chatty node cannot stall
        // the boot.
        void read_config_rom(FwNode &node, bool &is_irm) noexcept {
            is_irm = false;
            hcpp::u32 q[40];
            for (hcpp::u32 i = 0; i < 5; ++i) {
                if (!rom_quadlet(node.node_id, i, &q[i])) return;
            }
            const hcpp::u32 bus_info_len = (q[0] >> 24) & 0xF;
            if (bus_info_len < 4 || q[1] != 0x31333934u) return;  // "1394"
            node.guid = (hcpp::u64(q[3]) << 32) | q[4];
            // Bus characteristics: [31] irmc, [15:12] max_rec.
            is_irm = (q[2] & 0x80000000u) != 0;
            node.max_rec = static_cast<hcpp::u16>((q[2] >> 12) & 0xF);

            const hcpp::u32 root_dir = 1 + bus_info_len;
            if (root_dir + 1 >= 40) return;
            if (!rom_quadlet(node.node_id, root_dir, &q[0])) return;
            const hcpp::u32 dir_len = (q[0] >> 16) & 0xFFFF;
            const hcpp::u32 entries = dir_len < 32 ? dir_len : 32;
            for (hcpp::u32 i = 0; i < entries; ++i) {
                if (!rom_quadlet(node.node_id, root_dir + 1 + i, &q[i])) {
                    return;
                }
                const hcpp::u32 key_type = q[i] >> 30;
                const hcpp::u32 key_id = (q[i] >> 24) & 0x3F;
                if (key_type == 0) {  // immediate
                    if (key_id == 0x03) node.vendor_id = q[i] & 0xFFFFFF;
                    if (key_id == 0x17) node.model_id = q[i] & 0xFFFFFF;
                }
            }
            node.config_rom_ok = true;
        }

        void scan_nodes() noexcept {
            for (hcpp::u32 i = 0; i < node_count; ++i) {
                FwNode &n = nodes[i];
                if (!n.link_active || n.node_id == fw_local_node()) {
                    continue;
                }
                bool is_irm = false;
                read_config_rom(n, is_irm);
                if (is_irm && irm_node == 0xFF) {
                    irm_node = n.node_id;
                }
            }
        }

        // -----------------------------------------------------------
        // Iso receive context (packet-per-buffer, isochHeader on)
        // -----------------------------------------------------------

        // Deliver one completed IR buffer: quadlet 0 is the iso packet
        // header ([31:16] dataLength, [13:8] channel, [7:4] tcode,
        // [3:0] sy), payload follows, one timestamp quadlet trails.
        void ir_harvest_packets() noexcept {
            if (ir_ctx_index == fw_no_iso_ctx) return;
            for (hcpp::u32 n = 0; n < ir_buffers; ++n) {
                const hcpp::u32 i = (ir_harvest + n) % ir_buffers;
                volatile OhciDesc &d = ir_desc[i];
                volatile hcpp::u8 *buf = ir_buf + i * ir_buf_bytes;
                // A completed INPUT_LAST slot has res_count/status filled;
                // also accept a nonzero header quadlet as a fill marker.
                const hcpp::u32 first_q =
                    *reinterpret_cast<volatile hcpp::u32 *>(buf);
                if (d.res_count == 0 && d.transfer_status == 0 &&
                    first_q == 0) {
                    continue;
                }
                hcpp::u32 used =
                    d.res_count != 0 ? ir_buf_bytes - d.res_count
                                     : ir_buf_bytes;
                if (used > ir_buf_bytes) used = ir_buf_bytes;
                if (used >= 8) {
                    const hcpp::u32 hdr =
                        *reinterpret_cast<volatile hcpp::u32 *>(buf);
                    const hcpp::u32 data_len = (hdr >> 16) & 0xFFFF;
                    if (data_len <= used - 4) {
                        ++ir_packet_count;
                        if (rx_deliver != nullptr) {
                            rx_deliver(
                                const_cast<const hcpp::u8 *>(buf + 4),
                                data_len, rx_deliver_ctx);
                        }
                    }
                }
                ir_harvest = (i + 1) % ir_buffers;
                d.res_count = 0;
                d.transfer_status = 0;
            }
        }

        bool ir_context_start(hcpp::u8 ctx, hcpp::u8 channel) noexcept {
            for (hcpp::u32 i = 0; i < ir_buffers; ++i) {
                ir_desc[i].req_count = static_cast<hcpp::u16>(ir_buf_bytes);
                ir_desc[i].control = desc_input_last | desc_branch_always;
                ir_desc[i].data_address =
                    static_cast<hcpp::u32>(ir_buf_phys) + i * ir_buf_bytes;
                const hcpp::u32 next = (i + 1) % ir_buffers;
                ir_desc[i].branch_address =
                    (static_cast<hcpp::u32>(ir_desc_phys) +
                     next * sizeof(OhciDesc)) | 1;
                ir_desc[i].res_count = 0;
                ir_desc[i].transfer_status = 0;
            }
            // ContextMatch: [31] tag1Match? — OHCI format: [16:23]
            // channelMatch, [0] tag0Match for tag=1 (CIP).
            mmio_write(reg_ir_context_match(ctx),
                       (hcpp::u32(channel) << 16) | 1u);
            mmio_write(reg_ir_ctrl_clear(ctx), ~0u);
            if (!wait_flag(reg_ref(reg_ir_ctrl_set(ctx)), ctx_active, false,
                           100)) {
                return false;
            }
            mmio_write(reg_ir_cmd_ptr(ctx),
                       static_cast<hcpp::u32>(ir_desc_phys) | 1);
            barrier();
            mmio_write(reg_ir_ctrl_set(ctx), ctx_run | ir_isoch_header);
            return true;
        }

    }  // namespace

    // ---------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------

    bool fw_present() noexcept { return present; }
    hcpp::u32 fw_node_count() noexcept { return node_count; }
    const FwNode *fw_nodes() noexcept { return nodes; }
    const FwNode *fw_node(hcpp::u8 node_id) noexcept {
        for (hcpp::u32 i = 0; i < node_count; ++i) {
            if (nodes[i].node_id == node_id) return &nodes[i];
        }
        return nullptr;
    }
    hcpp::u16 fw_local_node_id() noexcept { return local_node_id; }
    hcpp::u8 fw_local_node() noexcept {
        return static_cast<hcpp::u8>(local_node_id & 0x3F);
    }
    hcpp::u32 fw_generation() noexcept { return generation; }
    hcpp::u32 fw_bus_reset_count() noexcept { return bus_resets; }
    hcpp::u32 fw_iso_rx_packets() noexcept { return ir_packet_count; }

    bool fw_read_quadlet(hcpp::u8 node, hcpp::u64 offset,
                         hcpp::u32 *value) noexcept {
        hcpp::u32 resp[4];
        const hcpp::u8 rc =
            transact(node, tcode_read_quadlet_request, offset, nullptr, 0, 0,
                     resp, 4, nullptr);
        if (rc != rcode_complete || value == nullptr) return false;
        // OHCI swaps header quadlets to host order but stores the data
        // quadlet in wire order — swap it back.
        *value = bswap32(resp[3]);
        return true;
    }

    bool fw_write_quadlet(hcpp::u8 node, hcpp::u64 offset,
                          hcpp::u32 value) noexcept {
        hcpp::u32 resp[4];
        return transact(node, tcode_write_quadlet_request, offset, nullptr, 0,
                        bswap32(value), resp, 4, nullptr) == rcode_complete;
    }

    bool fw_read_block(hcpp::u8 node, hcpp::u64 offset, void *data,
                       hcpp::u32 bytes) noexcept {
        if (data == nullptr || bytes == 0 || bytes > 512) return false;
        hcpp::u32 resp[4 + 128];
        hcpp::u32 resp_bytes = 0;
        const hcpp::u8 rc =
            transact(node, tcode_read_block_request, offset, nullptr, 0,
                     (bytes << 16), resp, 4 + 128, &resp_bytes);
        if (rc != rcode_complete) return false;
        // Payload quadlets arrive in wire order (BE per quadlet).
        hcpp::u8 *dst = static_cast<hcpp::u8 *>(data);
        const hcpp::u32 got = resp_bytes > 16 ? resp_bytes - 16 : 0;
        for (hcpp::u32 i = 0; i < got && i < bytes; ++i) {
            const hcpp::u32 quad = 4 + i / 4;
            dst[i] = static_cast<hcpp::u8>(resp[quad] >>
                                           ((3 - (i & 3)) * 8));
        }
        return got >= bytes;
    }

    bool fw_write_block(hcpp::u8 node, hcpp::u64 offset, const void *data,
                        hcpp::u32 bytes) noexcept {
        if (data == nullptr || bytes == 0 || bytes > 512) return false;
        hcpp::u32 resp[4];
        return transact(node, tcode_write_block_request, offset, data, bytes,
                        (bytes << 16), resp, 4, nullptr) == rcode_complete;
    }

    bool fw_lock_compare_swap(hcpp::u8 node, hcpp::u64 offset, hcpp::u32 arg,
                              hcpp::u32 data, hcpp::u32 *old) noexcept {
        hcpp::u32 lock_data[2] = {bswap32(arg), bswap32(data)};
        hcpp::u32 resp[8];
        const hcpp::u8 rc =
            transact(node, tcode_lock_request, offset, lock_data, 8,
                     (8u << 16) | ext_tcode_compare_swap, resp, 8, nullptr);
        if (rc != rcode_complete || old == nullptr) return false;
        *old = bswap32(resp[4]);  // lock result payload is wire-order
        return true;
    }

    hcpp::u8 fw_iso_rx_attach(hcpp::u8 channel, FwIsoRxFn deliver,
                            void *ctx) noexcept {
        if (!present || ir_ctx_index != fw_no_iso_ctx) return fw_no_iso_ctx;
        if (ir_buf_phys == 0 || ir_desc_phys == 0) return fw_no_iso_ctx;
        rx_deliver = deliver;
        rx_deliver_ctx = ctx;
        ir_harvest = 0;
        if (!ir_context_start(0, channel)) {
            rx_deliver = nullptr;
            return fw_no_iso_ctx;
        }
        ir_ctx_index = 0;
        return ir_ctx_index;
    }

    void fw_iso_rx_detach(hcpp::u8 ctx_index) noexcept {
        if (!present || ctx_index == fw_no_iso_ctx) return;
        mmio_write(reg_ir_ctrl_clear(ctx_index), ctx_run);
        rx_deliver = nullptr;
        ir_ctx_index = fw_no_iso_ctx;
    }

    void fw_poll() noexcept {
        if (!present) return;
        const hcpp::u32 events = mmio_read(reg_int_event_set);
        if (events != 0) {
            mmio_write(reg_int_event_clear, events);
            if ((events & int_bus_reset) != 0) {
                ++bus_resets;
            }
            if ((events & int_self_id_complete) != 0) {
                parse_self_id();
            }
        }
        ir_harvest_packets();
    }

    // ---------------------------------------------------------------
    // IRM (iso-resources.h)
    // ---------------------------------------------------------------

    hcpp::u8 fw_irm_node() noexcept { return irm_node; }
    hcpp::u64 fw_irm_channels_in_use() noexcept { return channels_in_use; }

    int fw_irm_alloc_channel() noexcept {
        for (hcpp::u32 ch = 0; ch < 64; ++ch) {
            if ((channels_in_use & (1ull << ch)) != 0) continue;
            if (irm_node != 0xFF) {
                const hcpp::u64 addr = ch < 32
                                           ? csr_channels_available_hi
                                           : csr_channels_available_lo;
                const hcpp::u32 bit = 1u << (31 - (ch & 31));
                hcpp::u32 old = 0;
                hcpp::u32 cur = 0;
                if (fw_read_quadlet(irm_node, addr, &cur)) {
                    if ((cur & bit) != 0 &&
                        fw_lock_compare_swap(irm_node, addr, cur,
                                             cur & ~bit, &old) &&
                        old == cur) {
                        channels_in_use |= 1ull << ch;
                        return static_cast<int>(ch);
                    }
                    continue;  // channel busy or contention
                }
            }
            // Local-only fallback (no IRM answered): claim the bit.
            channels_in_use |= 1ull << ch;
            return static_cast<int>(ch);
        }
        return -1;
    }

    bool fw_irm_free_channel(hcpp::u32 channel) noexcept {
        if (channel >= 64 || (channels_in_use & (1ull << channel)) == 0) {
            return false;
        }
        if (irm_node != 0xFF) {
            const hcpp::u64 addr = channel < 32
                                       ? csr_channels_available_hi
                                       : csr_channels_available_lo;
            const hcpp::u32 bit = 1u << (31 - (channel & 31));
            hcpp::u32 cur = 0;
            if (fw_read_quadlet(irm_node, addr, &cur)) {
                hcpp::u32 old = 0;
                fw_lock_compare_swap(irm_node, addr, cur, cur | bit, &old);
            }
        }
        channels_in_use &= ~(1ull << channel);
        return true;
    }

    hcpp::u32 fw_irm_bandwidth_units(hcpp::u8 speed,
                                   hcpp::u32 payload_bytes) noexcept {
        // ~20 ns quanta over a 125 us cycle = 6144 units; a byte takes
        // roughly 40 ns at S100, halving per speed step. Packet overhead
        // (header + crc + gap) is approximated as +64 B.
        const hcpp::u32 bytes = payload_bytes + 64;
        hcpp::u32 units = bytes * 2;
        units >>= speed;
        if (units == 0 || units > 6144) units = 6144;
        return units;
    }

    bool fw_irm_alloc_bandwidth(hcpp::u32 units) noexcept {
        if (units > 6144) return false;
        if (irm_node != 0xFF) {
            hcpp::u32 cur = 0;
            if (fw_read_quadlet(irm_node, csr_bandwidth_available, &cur)) {
                if (cur < units) return false;
                hcpp::u32 old = 0;
                if (fw_lock_compare_swap(irm_node, csr_bandwidth_available,
                                         cur, cur - units, &old) &&
                    old == cur) {
                    bandwidth_claimed += units;
                    return true;
                }
                return false;
            }
        }
        bandwidth_claimed += units;
        return true;
    }

    bool fw_irm_free_bandwidth(hcpp::u32 units) noexcept {
        if (units > bandwidth_claimed) return false;
        if (irm_node != 0xFF) {
            hcpp::u32 cur = 0;
            if (fw_read_quadlet(irm_node, csr_bandwidth_available, &cur)) {
                hcpp::u32 old = 0;
                fw_lock_compare_swap(irm_node, csr_bandwidth_available, cur,
                                     cur + units, &old);
            }
        }
        bandwidth_claimed -= units;
        return true;
    }

    // ---------------------------------------------------------------
    // Initialization
    // ---------------------------------------------------------------

    bool fw_initialize() noexcept {
        present = false;
        fail_step = 0;
        node_count = 0;
        local_node_id = 0;
        irm_node = 0xFF;
        generation = 0;
        bus_resets = 0;
        channels_in_use = 0;
        bandwidth_claimed = 0;
        txn_tlabel = 0;
        arrs_head = 0;
        ir_ctx_index = fw_no_iso_ctx;
        ir_harvest = 0;
        ir_packet_count = 0;
        rx_deliver = nullptr;
        rx_deliver_ctx = nullptr;
        ir_buf_phys = 0;
        ir_desc_phys = 0;

        // ---- PCI probe: serial bus / FireWire / OHCI ----------------
        const pci::PciDevice *dev = nullptr;
        for (hcpp::size_t i = 0; i < pci::device_count(); ++i) {
            const pci::PciDevice &d = pci::devices()[i];
            if (d.class_code == 0x0C && d.subclass == 0x00 &&
                d.program_interface == 0x10) {
                dev = &d;
                break;
            }
        }
        if (dev == nullptr || !memory::pmm_initialized()) {
            serial::write_line("fw dev=absent");
            serial::write_line("fw amdpt ch=none");
            serial::write_line("tascam dev=none");
            serial::write_line("fw iso rx=0 pkts");
            serial::write("amdtp codec=");
            serial::write_line(amdtp_codec_selftest() ? "ok" : "fail");
            return false;
        }

        const hcpp::u32 bar0 = pci::bar(*dev, 0);
        const hcpp::u64 mmio_phys = pci::bar_memory_address(bar0);
        if (!pci::bar_is_memory(bar0) || mmio_phys == 0 ||
            mmio_phys >= 0x100000000ull) {
            fail_step = 1;
            serial::write_line("fw dev=absent");
            return false;
        }
        pci::pci_enable_mem(*dev);
        pci::pci_enable_busmaster(*dev);
        mmio = reinterpret_cast<volatile hcpp::u8 *>(mmio_phys);

        // ---- DMA buffers ---------------------------------------------
        selfid_phys = memory::pmm_allocate_page();
        at_page_phys = memory::pmm_allocate_page();
        ar_desc_phys = memory::pmm_allocate_page();
        ar_buf_phys = memory::pmm_allocate_pages(2);   // 2 rings x 4 KiB
        ir_buf_phys = memory::pmm_allocate_pages(
            ir_buffers * ir_buf_bytes / memory::page_size);
        ir_desc_phys = memory::pmm_allocate_page();
        if (selfid_phys == 0 || at_page_phys == 0 || ar_desc_phys == 0 ||
            ar_buf_phys == 0 || ir_buf_phys == 0 || ir_desc_phys == 0) {
            fail_step = 2;
            serial::write_line("fw dev=absent");
            return false;
        }
        selfid_buf = reinterpret_cast<volatile hcpp::u32 *>(selfid_phys);
        at_desc = reinterpret_cast<volatile OhciDesc *>(at_page_phys);
        ar_desc = reinterpret_cast<volatile OhciDesc *>(ar_desc_phys);
        ar_buf = reinterpret_cast<volatile hcpp::u8 *>(ar_buf_phys);
        ir_buf = reinterpret_cast<volatile hcpp::u8 *>(ir_buf_phys);
        ir_desc = reinterpret_cast<volatile OhciDesc *>(ir_desc_phys);
        hcpp::set(const_cast<hcpp::u32 *>(selfid_buf), 0, memory::page_size);
        hcpp::set(const_cast<OhciDesc *>(at_desc), 0, memory::page_size);
        hcpp::set(const_cast<OhciDesc *>(ar_desc), 0, memory::page_size);
        hcpp::set(const_cast<hcpp::u8 *>(ar_buf), 0,
                  2 * memory::page_size);
        hcpp::set(const_cast<OhciDesc *>(ir_desc), 0, memory::page_size);
        hcpp::set(const_cast<hcpp::u8 *>(ir_buf), 0,
                  ir_buffers * ir_buf_bytes);

        // ---- Controller reset + link bring-up ------------------------
        mmio_write(reg_hccontrol_set, hccontrol_soft_reset);
        if (!wait_flag(reg_ref(reg_hccontrol_set), hccontrol_soft_reset,
                       false, 200)) {
            fail_step = 3;
            serial::write_line("fw dev=absent");
            return false;
        }
        mmio_write(reg_hccontrol_set, hccontrol_lps);
        if (!wait_flag(reg_ref(reg_hccontrol_set), hccontrol_lps, true,
                       200)) {
            fail_step = 4;
            serial::write_line("fw dev=absent");
            return false;
        }

        mmio_write(reg_self_id_buffer,
                   static_cast<hcpp::u32>(selfid_phys));
        mmio_write(reg_phy_upper_bound, 0x00010000u);
        mmio_write(reg_at_retries, 0x00000fffu);
        // Accept async/physical requests for any node id (we are the
        // only requester; responses are gated by destination id in the
        // packet itself).
        mmio_write(reg_as_req_filter_hi_set, ~0u);
        mmio_write(reg_as_req_filter_lo_set, ~0u);
        mmio_write(reg_phy_req_filter_hi_set, ~0u);
        mmio_write(reg_phy_req_filter_lo_set, ~0u);
        mmio_write(reg_int_mask_set, int_master_enable |
                                       int_self_id_complete |
                                       int_bus_reset | int_rsp_pkt |
                                       int_isoch_rx);
        // Report the implemented iso contexts (mask registers read back
        // the supported bits).
        const hcpp::u32 it_ctx = mmio_read(reg_iso_xmit_int_mask_set);
        const hcpp::u32 ir_ctx = mmio_read(reg_iso_recv_int_mask_set);
        serial::write("fw iso ctx it=");
        serial::write_dec32(static_cast<hcpp::u32>(
            __builtin_popcount(it_ctx)));
        serial::write(" ir=");
        serial::write_dec32(static_cast<hcpp::u32>(
            __builtin_popcount(ir_ctx)));
        serial::write("\r\n");

        if (!ar_context_start(reg_arrq_ctrl_set, reg_arrq_ctrl_clear,
                              reg_arrq_cmd_ptr, 0, 0) ||
            !ar_context_start(reg_arrs_ctrl_set, reg_arrs_ctrl_clear,
                              reg_arrs_cmd_ptr, ar_buffers,
                              memory::page_size)) {
            fail_step = 5;
            serial::write_line("fw dev=absent");
            return false;
        }

        mmio_write(reg_hccontrol_set,
                   hccontrol_posted_write | hccontrol_link_enable |
                       hccontrol_phy_enable);
        mmio_write(reg_link_control_set,
                   link_rcv_self_id | link_rcv_phy_pkt |
                       link_cycle_timer_enable);

        // Enabling the link triggers a bus reset; the self-ID stream is
        // DMA'd into the self-id buffer and selfIDComplete is flagged.
        if (!wait_flag(reg_ref(reg_int_event_set), int_self_id_complete,
                       true, 300)) {
            fail_step = 6;
            serial::write_line("fw dev=absent");
            return false;
        }
        mmio_write(reg_int_event_clear, int_self_id_complete);
        const hcpp::u32 node_id_reg = mmio_read(reg_node_id);
        if ((node_id_reg & node_id_valid) == 0) {
            fail_step = 7;
            serial::write_line("fw dev=absent");
            return false;
        }
        local_node_id = static_cast<hcpp::u16>(node_id_reg & 0xFFFF);
        parse_self_id();
        present = true;  // link up: transactions + iso attach allowed
        serial::write("fw dev=ok nodes=");
        serial::write_dec32(node_count);
        serial::write("\r\n");
        if ((node_id_reg & node_id_root) != 0) {
            serial::write_line("fw node=root");
        }

        // ---- Config-ROM walk -----------------------------------------
        scan_nodes();
        serial::write("fw irm=");
        if (irm_node != 0xFF) {
            serial::write_dec32(irm_node);
        } else {
            serial::write("none");
        }
        serial::write("\r\n");

        // ---- TASCAM match + AMDTP capture path ------------------------
        const TascamDevKey *tascam = nullptr;
        hcpp::u8 tascam_node = 0;
        for (hcpp::u32 i = 0; i < node_count; ++i) {
            const FwNode &n = nodes[i];
            const TascamDevKey *hit =
                tascam_match(n.vendor_id, n.model_id);
            if (hit != nullptr) {
                tascam = hit;
                tascam_node = n.node_id;
                break;
            }
        }
        if (tascam == nullptr) {
            serial::write_line("tascam dev=none");
            serial::write_line("fw amdpt ch=none");
        } else {
            serial::write("tascam dev=");
            serial::write_line(tascam->name);
            const int channel = fw_irm_alloc_channel();
            serial::write("fw amdpt ch=");
            if (channel < 0) {
                serial::write_line("none");
            } else {
                serial::write_dec32(static_cast<hcpp::u32>(channel));
                serial::write("\r\n");
                // Capture at 48 kHz: every TASCAM unit supports it.
                if (tascam_capture_start(tascam, tascam_node, 48000)) {
                    static AmdtpStream capture_stream;
                    if (amdtp_stream_open(capture_stream, AmdtpDir::Rx,
                                          tascam->capture_channels,
                                          tascam->midi_ports, 48000,
                                          fw_local_node()) &&
                        fw_iso_pipe_open(capture_stream,
                                         static_cast<hcpp::u8>(channel),
                                         tascam_node) != nullptr) {
                        // Let a few iso cycles elapse so packets can
                        // land, then report what arrived.
                        interrupts::pit_wait_ticks(30);
                        fw_poll();
                    }
                }
            }
        }
        serial::write("fw iso rx=");
        serial::write_dec32(ir_packet_count);
        serial::write(" pkts\r\n");

        serial::write("amdtp codec=");
        serial::write_line(amdtp_codec_selftest() ? "ok" : "fail");
        return true;
    }

}  // namespace hype::fw
