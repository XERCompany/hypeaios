#pragma once

// IEEE 1394 bus services built on an OHCI host controller (PCI class
// 0x0C, subclass 0x00, prog-if 0x10). fw_initialize() is the runtime
// stage body ("firewire.ohci", priority 58): it needs pci.scan +
// memory.pmm + interrupts.foundation + time.timer, never fails the
// boot, and prints the serial markers
//   "fw dev=absent"           no OHCI PCI device / unusable BAR / dead link
//   "fw dev=ok nodes=N"       link up, N nodes seen in the self-ID stream
//   "fw amdpt ch=N"           isochronous channel allocated for AMDTP
//   "tascam dev=<model>"      a known TASCAM unit answered on the bus
//   "fw iso rx=N pkts"        iso packets delivered by the IR context
//   "amdtp codec=ok"          AMDTP/AM824 codec round-trip selftest
//
// The quadlet/block/lock request-response model talks to node CSR space
// (config ROM at CSR_CONFIG_ROM, IRM registers at CSR_BASE+0x200) via the
// async transmit context (ATRQ) with responses DMA'd through the async
// receive context (ARRS); completion is polled because PCI INTx is not
// routed for this device.

#include "hcpp/types.hpp"

namespace hype::fw {

    // ---- IEEE 1394 transaction codes ----------------------------------
    constexpr hcpp::u8 tcode_write_quadlet_request = 0x0;
    constexpr hcpp::u8 tcode_write_block_request = 0x1;
    constexpr hcpp::u8 tcode_write_response = 0x2;
    constexpr hcpp::u8 tcode_read_quadlet_request = 0x4;
    constexpr hcpp::u8 tcode_read_block_request = 0x5;
    constexpr hcpp::u8 tcode_read_quadlet_response = 0x6;
    constexpr hcpp::u8 tcode_read_block_response = 0x7;
    constexpr hcpp::u8 tcode_cycle_start = 0x8;
    constexpr hcpp::u8 tcode_lock_request = 0x9;
    constexpr hcpp::u8 tcode_stream_data = 0xA;
    constexpr hcpp::u8 tcode_lock_response = 0xB;

    // Extended tcodes carried in quadlet 3 of lock transactions.
    constexpr hcpp::u8 ext_tcode_mask_swap = 0x1;
    constexpr hcpp::u8 ext_tcode_compare_swap = 0x2;
    constexpr hcpp::u8 ext_tcode_fetch_add = 0x3;
    constexpr hcpp::u8 ext_tcode_little_add = 0x4;
    constexpr hcpp::u8 ext_tcode_bounded_add = 0x5;
    constexpr hcpp::u8 ext_tcode_wrap_add = 0x6;

    // Response codes carried in responses (and OHCI acks).
    constexpr hcpp::u8 rcode_complete = 0x0;
    constexpr hcpp::u8 rcode_conflict_error = 0x4;
    constexpr hcpp::u8 rcode_data_error = 0x5;
    constexpr hcpp::u8 rcode_type_error = 0x6;
    constexpr hcpp::u8 rcode_address_error = 0x7;
    constexpr hcpp::u8 rcode_send_error = 0x10;
    constexpr hcpp::u8 rcode_cancelled = 0x11;
    constexpr hcpp::u8 rcode_busy = 0x12;
    constexpr hcpp::u8 rcode_generation_error = 0x13;
    constexpr hcpp::u8 rcode_no_ack = 0x14;

    // Link speeds (1394a/b S-codes used in self-ID and packet headers).
    constexpr hcpp::u8 speed_s100 = 0;
    constexpr hcpp::u8 speed_s200 = 1;
    constexpr hcpp::u8 speed_s400 = 2;
    constexpr hcpp::u8 speed_s800 = 3;

    // CSR address space (IEEE 1394 §8.3). Node offsets are relative to a
    // node's private space; FFFF F000 0000 is the canonical CSR base.
    constexpr hcpp::u64 csr_base = 0xFFFFF0000000ull;
    constexpr hcpp::u64 csr_state_clear = csr_base + 0x00;
    constexpr hcpp::u64 csr_node_ids = csr_base + 0x08;
    constexpr hcpp::u64 csr_cycle_time = csr_base + 0x200;
    constexpr hcpp::u64 csr_bus_manager_id = csr_base + 0x21C;
    constexpr hcpp::u64 csr_bandwidth_available = csr_base + 0x220;
    constexpr hcpp::u64 csr_channels_available_hi = csr_base + 0x224;
    constexpr hcpp::u64 csr_channels_available_lo = csr_base + 0x228;
    constexpr hcpp::u64 csr_config_rom = csr_base + 0x400;
    constexpr hcpp::u64 csr_topology_map = csr_base + 0x1000;

    constexpr hcpp::u8 broadcast_node = 0x3F;
    constexpr hcpp::size_t fw_max_nodes = 64;

    struct FwNode {
        hcpp::u8 node_id;      // 6-bit node number on the local bus
        bool link_active;      // L bit in the node's self-ID packet
        hcpp::u8 speed;        // negotiated S-code from self-ID
        hcpp::u8 port_status[4];  // p0..p3: 0 none, 1 parent, 2 child
        hcpp::u32 vendor_id;   // root-dir Vendor_ID immediate (24-bit OUI)
        hcpp::u32 model_id;    // root-dir Model_ID immediate
        hcpp::u64 guid;        // EUI-64 from the bus-info block
        hcpp::u16 max_rec;     // max receive quadlets from BusOptions-ish
        bool self_id_seen;
        bool config_rom_ok;
    };

    // Runtime-stage entry point. Probes PCI, brings up the link, waits
    // for the self-ID stream, reads every node's config ROM, allocates
    // an iso channel and, when a TASCAM-class unit is present, arms an
    // IR context delivering AMDTP packets. Always returns so the boot
    // cannot fail; the bool reports whether a live link came up.
    bool fw_initialize() noexcept;
    bool fw_present() noexcept;
    hcpp::u32 fw_node_count() noexcept;
    const FwNode *fw_nodes() noexcept;
    const FwNode *fw_node(hcpp::u8 node_id) noexcept;
    hcpp::u16 fw_local_node_id() noexcept;  // 16-bit (bus << 6) | node
    hcpp::u8 fw_local_node() noexcept;
    hcpp::u32 fw_generation() noexcept;
    hcpp::u32 fw_bus_reset_count() noexcept;
    hcpp::u32 fw_iso_rx_packets() noexcept;

    // ---- Async CSR transactions ---------------------------------------
    //
    // `node` is the 6-bit node id on the local bus. `offset` is the full
    // 48-bit 1394 address (CSR_BASE-relative). All calls are synchronous
    // from the caller's point of view: the request is queued on the ATRQ
    // context and the matching response packet is pulled off the ARRS
    // context with a bounded PIT-tick wait. tLabel matching is internal.
    bool fw_read_quadlet(hcpp::u8 node, hcpp::u64 offset,
                         hcpp::u32 *value) noexcept;
    bool fw_write_quadlet(hcpp::u8 node, hcpp::u64 offset,
                          hcpp::u32 value) noexcept;
    bool fw_read_block(hcpp::u8 node, hcpp::u64 offset, void *data,
                       hcpp::u32 bytes) noexcept;
    bool fw_write_block(hcpp::u8 node, hcpp::u64 offset, const void *data,
                        hcpp::u32 bytes) noexcept;
    bool fw_lock_compare_swap(hcpp::u8 node, hcpp::u64 offset, hcpp::u32 arg,
                              hcpp::u32 data, hcpp::u32 *old) noexcept;

    // ---- Iso plumbing used by amdtpstreampipe --------------------------
    //
    // fw_iso_rx_attach() programs an IR context in packet-per-buffer mode
    // (isochHeader on) matching `channel`, and returns its context index.
    // Buffers form a ring; fw_poll() scans the ring, counts completed
    // packets in fw_iso_rx_packets() and hands each one (iso header
    // quadlet + CIP payload) to the registered AMDTP consumer.
    constexpr hcpp::u8 fw_no_iso_ctx = 0xFF;
    using FwIsoRxFn = void (*)(const hcpp::u8 *packet, hcpp::u32 bytes,
                               void *ctx) noexcept;
    hcpp::u8 fw_iso_rx_attach(hcpp::u8 channel, FwIsoRxFn deliver,
                            void *ctx) noexcept;
    void fw_iso_rx_detach(hcpp::u8 ctx_index) noexcept;

    // Idle-loop drain: bus-reset re-scan + iso ring harvest + async
    // response reaping. Cheap when nothing is armed.
    void fw_poll() noexcept;

}  // namespace hype::fw
