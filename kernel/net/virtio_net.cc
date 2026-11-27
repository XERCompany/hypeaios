#include "kernel/net/virtio_net.h"

#include "hcpp/memory.hpp"
#include "kernel/arch/x86/io.h"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        constexpr hcpp::u16 virtio_vendor = 0x1AF4;
        constexpr hcpp::u16 virtio_net_legacy_id = 0x1000;
        constexpr hcpp::u16 virtio_net_modern_id = 0x1041;

        // Legacy virtio-pci common configuration registers: byte offsets
        // from the device I/O BAR.
        constexpr hcpp::u16 reg_device_features = 0x00;  // u32 ro
        constexpr hcpp::u16 reg_guest_features = 0x04;   // u32 wo
        constexpr hcpp::u16 reg_queue_address = 0x08;    // u32 wo, guest PFN
        constexpr hcpp::u16 reg_queue_size = 0x0C;       // u16 ro
        constexpr hcpp::u16 reg_queue_select = 0x0E;     // u16 wo
        constexpr hcpp::u16 reg_queue_notify = 0x10;     // u16 wo
        constexpr hcpp::u16 reg_device_status = 0x12;    // u8 rw
        constexpr hcpp::u16 reg_isr_status = 0x13;       // u8 ro
        constexpr hcpp::u16 reg_device_config = 0x14;    // MAC at offset +0

        constexpr hcpp::u8 status_acknowledge = 1;
        constexpr hcpp::u8 status_driver = 2;
        constexpr hcpp::u8 status_driver_ok = 4;
        constexpr hcpp::u8 status_features_ok = 8;
        constexpr hcpp::u8 status_failed = 128;

        constexpr hcpp::u16 queue_rx = 0;
        constexpr hcpp::u16 queue_tx = 1;

        constexpr hcpp::u16 desc_flag_write = 2;

        constexpr hcpp::u16 rx_buffer_max = 32;
        constexpr hcpp::u16 tx_slot_max = 16;
        // virtio_net_hdr + a full 1518-byte frame fit with room to spare.
        constexpr hcpp::size_t buffer_size = 2048;
        // No offload features are negotiated (guest_features = 0), so the
        // device uses the 10-byte legacy virtio_net_hdr, not the 12-byte
        // virtio 1.0 one.
        constexpr hcpp::size_t net_hdr_size = 10;

        struct VirtqDesc {
            hcpp::u64 addr;
            hcpp::u32 len;
            hcpp::u16 flags;
            hcpp::u16 next;
        };
        static_assert(sizeof(VirtqDesc) == 16);

        struct VirtqUsedElem {
            hcpp::u32 id;
            hcpp::u32 len;
        };

        struct VirtioNetHdr {
            hcpp::u8 flags;
            hcpp::u8 gso_type;
            hcpp::u16 hdr_len;
            hcpp::u16 gso_size;
            hcpp::u16 csum_start;
            hcpp::u16 csum_offset;
        } __attribute__((packed));
        static_assert(sizeof(VirtioNetHdr) == net_hdr_size);

        // Split vring (legacy layout): descriptor table, then the avail
        // ring, then the used ring at the next page boundary.
        struct VirtQueue {
            volatile VirtqDesc* desc;
            volatile hcpp::u16* avail_flags;
            volatile hcpp::u16* avail_idx;
            volatile hcpp::u16* avail_ring;
            volatile hcpp::u16* used_flags;
            volatile hcpp::u16* used_idx;
            volatile VirtqUsedElem* used_ring;
            hcpp::uintptr_t phys;
            hcpp::size_t pages;
            hcpp::u16 size;
            hcpp::u16 last_used;
        };

        bool present;
        hcpp::u16 iobase;
        hcpp::u8 mac[6];
        VirtQueue rxq;
        VirtQueue txq;
        hcpp::uintptr_t rx_pool;
        hcpp::uintptr_t tx_pool;
        hcpp::u16 rx_count;
        hcpp::u16 tx_free[tx_slot_max];
        hcpp::u16 tx_free_count;

        void fence() noexcept { asm volatile("mfence" ::: "memory"); }

        hcpp::u8 status_read() noexcept { return hype_port_in8(iobase + reg_device_status); }
        void status_write(hcpp::u8 value) noexcept {
            hype_port_out8(iobase + reg_device_status, value);
        }

        void avail_push(VirtQueue& q, hcpp::u16 desc_id) noexcept {
            const hcpp::u16 idx = *q.avail_idx;
            q.avail_ring[idx % q.size] = desc_id;
            fence();
            *q.avail_idx = static_cast<hcpp::u16>(idx + 1);
        }

        void notify(hcpp::u16 queue) noexcept {
            hype_port_out16(iobase + reg_queue_notify, queue);
        }

        bool queue_setup(hcpp::u16 index, VirtQueue& q) noexcept {
            hype_port_out16(iobase + reg_queue_select, index);
            const hcpp::u16 size = hype_port_in16(iobase + reg_queue_size);
            if (size == 0 || (size & (size - 1)) != 0) return false;
            q.size = size;
            q.last_used = 0;
            const hcpp::size_t desc_avail_bytes =
                16 * hcpp::size_t(size) + 6 + 2 * hcpp::size_t(size);
            const hcpp::size_t used_offset =
                (desc_avail_bytes + memory::page_size - 1) & ~(memory::page_size - 1);
            const hcpp::size_t total = used_offset + 6 + 8 * hcpp::size_t(size);
            q.pages = (total + memory::page_size - 1) / memory::page_size;
            q.phys = memory::pmm_allocate_pages(q.pages);
            if (q.phys == 0) return false;
            hcpp::u8* base = reinterpret_cast<hcpp::u8*>(q.phys);
            hcpp::set(base, 0, q.pages * memory::page_size);
            q.desc = reinterpret_cast<volatile VirtqDesc*>(base);
            q.avail_flags = reinterpret_cast<volatile hcpp::u16*>(base + 16 * size);
            q.avail_idx = q.avail_flags + 1;
            q.avail_ring = q.avail_flags + 2;
            q.used_flags =
                reinterpret_cast<volatile hcpp::u16*>(base + used_offset);
            q.used_idx = q.used_flags + 1;
            q.used_ring =
                reinterpret_cast<volatile VirtqUsedElem*>(base + used_offset + 4);
            hype_port_out32(iobase + reg_queue_address,
                            static_cast<hcpp::u32>(q.phys >> 12));
            return true;
        }

        void rx_post(hcpp::u16 slot) noexcept {
            rxq.desc[slot].addr = rx_pool + hcpp::u64(slot) * buffer_size;
            rxq.desc[slot].len = buffer_size;
            rxq.desc[slot].flags = desc_flag_write;
            rxq.desc[slot].next = 0;
            avail_push(rxq, slot);
        }

        void tx_reclaim() noexcept {
            if (txq.last_used == *txq.used_idx) return;
            fence();
            while (txq.last_used != *txq.used_idx) {
                const hcpp::u32 id = txq.used_ring[txq.last_used % txq.size].id;
                ++txq.last_used;
                if (id < tx_slot_max && tx_free_count < tx_slot_max)
                    tx_free[tx_free_count++] = static_cast<hcpp::u16>(id);
            }
        }

        void write_mac() noexcept {
            static const char hex[] = "0123456789abcdef";
            for (hcpp::u8 i = 0; i < 6; ++i) {
                if (i != 0) serial::write_char(':');
                serial::write_char(hex[mac[i] >> 4]);
                serial::write_char(hex[mac[i] & 0xF]);
            }
        }

        void fail() noexcept {
            status_write(status_failed);
            serial::write_line("virtio-net dev=fail");
        }

    }  // namespace

    bool virtio_net_probe() noexcept {
        present = false;
        const pci::PciDevice* dev = pci::find(virtio_vendor, virtio_net_legacy_id);
        if (dev == nullptr) dev = pci::find(virtio_vendor, virtio_net_modern_id);
        if (dev == nullptr) {
            serial::write_line("virtio-net dev=absent");
            return false;
        }
        iobase = 0;
        for (hcpp::u8 i = 0; i < 6; ++i) {
            const hcpp::u32 bar = pci::bar(*dev, i);
            if (pci::bar_is_io(bar)) {
                iobase = pci::bar_io_address(bar);
                break;
            }
        }
        if (iobase == 0) {
            // Transitional device without a legacy I/O BAR (modern-only
            // cap list). The modern transport is not implemented yet, so
            // treat it as absent rather than hang the boot.
            serial::write_line("virtio-net dev=absent");
            return false;
        }
        pci::pci_enable_io(*dev);
        pci::pci_enable_busmaster(*dev);

        // Reset: write 0 to device status and wait for it to read back 0.
        status_write(0);
        for (hcpp::u32 spin = 0; spin < 0x100000 && status_read() != 0; ++spin) {
        }
        if (status_read() != 0) {
            serial::write_line("virtio-net dev=absent");
            return false;
        }
        status_write(status_acknowledge);
        status_write(status_acknowledge | status_driver);

        // Negotiate no feature bits: no checksum/TSO offloading, no
        // VERSION_1 — the legacy path plus software checksumming is all
        // the tiny stack needs.
        (void)hype_port_in32(iobase + reg_device_features);
        hype_port_out32(iobase + reg_guest_features, 0);
        status_write(status_acknowledge | status_driver | status_features_ok);
        if ((status_read() & status_features_ok) == 0) {
            fail();
            return false;
        }

        for (hcpp::u8 i = 0; i < 6; ++i)
            mac[i] = hype_port_in8(iobase + reg_device_config + i);

        if (!queue_setup(queue_rx, rxq) || !queue_setup(queue_tx, txq)) {
            fail();
            return false;
        }
        rx_count = rxq.size < rx_buffer_max ? rxq.size : rx_buffer_max;
        const hcpp::u16 tx_slots = txq.size < tx_slot_max ? txq.size : tx_slot_max;
        rx_pool = memory::pmm_allocate_pages(
            (hcpp::size_t(rx_count) * buffer_size + memory::page_size - 1) /
            memory::page_size);
        tx_pool = memory::pmm_allocate_pages(
            (hcpp::size_t(tx_slots) * buffer_size + memory::page_size - 1) /
            memory::page_size);
        if (rx_pool == 0 || tx_pool == 0) {
            fail();
            return false;
        }
        tx_free_count = 0;
        for (hcpp::u16 i = 0; i < tx_slots; ++i) tx_free[tx_free_count++] = i;

        status_write(status_acknowledge | status_driver | status_features_ok |
                     status_driver_ok);
        present = true;

        // Pre-post all RX buffers before any traffic can arrive.
        for (hcpp::u16 i = 0; i < rx_count; ++i) rx_post(i);
        notify(queue_rx);

        serial::write("virtio-net dev=ok mac=");
        write_mac();
        serial::write("\r\n");
        return true;
    }

    bool virtio_net_ready() noexcept { return present; }

    const hcpp::u8* virtio_net_mac() noexcept { return mac; }

    bool virtio_net_send(const void* frame, hcpp::size_t length) noexcept {
        if (!present || frame == nullptr || length == 0 ||
            length + net_hdr_size > buffer_size)
            return false;
        tx_reclaim();
        if (tx_free_count == 0) return false;
        const hcpp::u16 slot = tx_free[--tx_free_count];
        hcpp::u8* buf = reinterpret_cast<hcpp::u8*>(
            tx_pool + hcpp::u64(slot) * buffer_size);
        VirtioNetHdr* hdr = reinterpret_cast<VirtioNetHdr*>(buf);
        hdr->flags = 0;
        hdr->gso_type = 0;
        hdr->hdr_len = 0;
        hdr->gso_size = 0;
        hdr->csum_start = 0;
        hdr->csum_offset = 0;
        hcpp::copy(buf + net_hdr_size, frame, length);
        txq.desc[slot].addr = tx_pool + hcpp::u64(slot) * buffer_size;
        txq.desc[slot].len = static_cast<hcpp::u32>(net_hdr_size + length);
        txq.desc[slot].flags = 0;
        txq.desc[slot].next = 0;
        avail_push(txq, slot);
        notify(queue_tx);
        return true;
    }

    hcpp::size_t virtio_net_recv(void* frame, hcpp::size_t capacity) noexcept {
        if (!present || frame == nullptr) return 0;
        // Reading the ISR status acknowledges a pending interrupt; we are
        // poll-driven so no handler needs it, but the device expects it.
        (void)hype_port_in8(iobase + reg_isr_status);
        if (rxq.last_used == *rxq.used_idx) return 0;
        fence();
        const hcpp::u32 elem_id = rxq.used_ring[rxq.last_used % rxq.size].id;
        const hcpp::u32 elem_len = rxq.used_ring[rxq.last_used % rxq.size].len;
        ++rxq.last_used;
        if (elem_id >= rx_count) return 0;
        const hcpp::u8* buf = reinterpret_cast<const hcpp::u8*>(
            rx_pool + hcpp::u64(elem_id) * buffer_size);
        hcpp::size_t length =
            elem_len > net_hdr_size ? elem_len - net_hdr_size : 0;
        if (length > capacity) length = capacity;
        hcpp::copy(frame, buf + net_hdr_size, length);
        rx_post(static_cast<hcpp::u16>(elem_id));
        notify(queue_rx);
        return length;
    }

}  // namespace hype::net
