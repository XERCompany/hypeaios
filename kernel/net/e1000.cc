#include "kernel/net/e1000.h"

#include "hcpp/memory.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/pci/pci_irq.h"
#include "kernel/serial.hpp"

namespace hype::net {
    namespace {

        constexpr hcpp::u16 intel_vendor = 0x8086;
        // 82540EM/82545EM/82546 family — the classic "e1000" QEMU
        // device is 0x100E; the rest are close relatives sharing the
        // same BAR0 register file and legacy descriptor format.
        constexpr hcpp::u16 e1000_device_ids[] = {
            0x100E,  // 82540EM (QEMU -device e1000)
            0x100F,  // 82545EM
            0x1010,  // 82546EB (dual port)
            0x1011,  // 82545EM fiber
            0x1015,  // 82540EM mobile
            0x1016,  // 82540EP mobile
            0x1017,  // 82540EP
            0x101E,  // 82540EP mobile
            0x1026,  // 82545GM
            0x1078,  // 82541EI
        };

        // BAR0 register offsets (82540EM SDM, 32-bit registers).
        constexpr hcpp::u32 reg_ctrl = 0x0000;
        constexpr hcpp::u32 reg_status = 0x0008;
        constexpr hcpp::u32 reg_eerd = 0x0014;
        constexpr hcpp::u32 reg_icr = 0x00C0;  // read-to-clear
        constexpr hcpp::u32 reg_ims = 0x00D0;
        constexpr hcpp::u32 reg_imc = 0x00D8;
        constexpr hcpp::u32 reg_rctl = 0x0100;
        constexpr hcpp::u32 reg_tctl = 0x0400;
        constexpr hcpp::u32 reg_tipg = 0x0410;
        constexpr hcpp::u32 reg_rdbal = 0x2800;
        constexpr hcpp::u32 reg_rdbah = 0x2804;
        constexpr hcpp::u32 reg_rdlen = 0x2808;
        constexpr hcpp::u32 reg_rdh = 0x2810;
        constexpr hcpp::u32 reg_rdt = 0x2818;
        constexpr hcpp::u32 reg_tdbal = 0x3800;
        constexpr hcpp::u32 reg_tdbah = 0x3804;
        constexpr hcpp::u32 reg_tdlen = 0x3808;
        constexpr hcpp::u32 reg_tdh = 0x3810;
        constexpr hcpp::u32 reg_tdt = 0x3818;
        constexpr hcpp::u32 reg_mta = 0x5200;  // 128 x u32 multicast table
        constexpr hcpp::u32 reg_ral0 = 0x5400;
        constexpr hcpp::u32 reg_rah0 = 0x5404;

        constexpr hcpp::u32 ctrl_slu = 1u << 6;   // set link up
        constexpr hcpp::u32 ctrl_rst = 1u << 26;  // full device reset

        constexpr hcpp::u32 status_lu = 1u << 1;  // link up

        constexpr hcpp::u32 eerd_start = 1u;
        constexpr hcpp::u32 eerd_done = 1u << 4;

        constexpr hcpp::u32 icr_txdw = 1u << 0;
        constexpr hcpp::u32 icr_lsc = 1u << 2;
        constexpr hcpp::u32 icr_rxdmt0 = 1u << 4;
        constexpr hcpp::u32 icr_rxo = 1u << 6;
        constexpr hcpp::u32 icr_rxt0 = 1u << 7;

        constexpr hcpp::u32 rctl_en = 1u << 1;
        constexpr hcpp::u32 rctl_sbp = 1u << 2;    // store bad packets
        constexpr hcpp::u32 rctl_upe = 1u << 3;    // unicast promiscuous
        constexpr hcpp::u32 rctl_mpe = 1u << 4;    // multicast promiscuous
        constexpr hcpp::u32 rctl_bam = 1u << 15;   // broadcast accept
        // BSIZE bits 16:17 = 0 -> 2048-byte buffers; SECRC strips the
        // Ethernet CRC so frames reach the stack at the FCS boundary.
        constexpr hcpp::u32 rctl_secrc = 1u << 26;

        constexpr hcpp::u32 tctl_en = 1u << 1;
        constexpr hcpp::u32 tctl_psp = 1u << 3;    // pad short packets
        constexpr hcpp::u32 tctl_ct = 0x10u << 4;  // collision threshold
        constexpr hcpp::u32 tctl_cold = 0x40u << 12;  // full-duplex distance
        constexpr hcpp::u32 tipg_82540 = 0x0060200Au;  // IPGT/IPGR1/IPGR2

        constexpr hcpp::u32 rah_av = 1u << 31;  // RAL0/RAH0 MAC valid

        // Legacy RX descriptor (16 bytes).
        struct E1000RxDesc {
            hcpp::u64 addr;
            hcpp::u16 length;
            hcpp::u16 checksum;
            hcpp::u8 status;
            hcpp::u8 errors;
            hcpp::u16 special;
        };
        static_assert(sizeof(E1000RxDesc) == 16);

        // Legacy TX descriptor (16 bytes).
        struct E1000TxDesc {
            hcpp::u64 addr;
            hcpp::u16 length;
            hcpp::u8 cso;
            hcpp::u8 cmd;
            hcpp::u8 status;
            hcpp::u8 css;
            hcpp::u16 special;
        };
        static_assert(sizeof(E1000TxDesc) == 16);

        constexpr hcpp::u8 rx_status_dd = 1u << 0;
        constexpr hcpp::u8 tx_cmd_eop = 1u << 0;
        constexpr hcpp::u8 tx_cmd_ifcs = 1u << 1;  // insert FCS
        constexpr hcpp::u8 tx_cmd_rs = 1u << 3;    // report status
        constexpr hcpp::u8 tx_status_dd = 1u << 0;

        constexpr hcpp::u16 rx_desc_count = 32;
        constexpr hcpp::u16 tx_desc_count = 32;
        constexpr hcpp::size_t buffer_size = 2048;  // matches RCTL.BSIZE=0

        constexpr hcpp::u32 spin_busy = 0x100000;  // bounded wait bound

        bool present;
        bool irq_armed;
        volatile hcpp::u8* bar0;
        hcpp::u8 mac[6];

        volatile E1000RxDesc* rx_descs;
        volatile E1000TxDesc* tx_descs;
        hcpp::uintptr_t rx_ring_phys;
        hcpp::uintptr_t tx_ring_phys;
        hcpp::uintptr_t rx_pool;
        hcpp::uintptr_t tx_pool;
        hcpp::u16 rx_cursor;  // software shadow of RDH

        hcpp::u32 stat_rx;
        hcpp::u32 stat_tx;
        hcpp::u32 stat_irqs;

        void fence() noexcept { asm volatile("mfence" ::: "memory"); }

        hcpp::u32 mmio_read(hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32*>(bar0 + offset);
        }
        void mmio_write(hcpp::u32 offset, hcpp::u32 value) noexcept {
            *reinterpret_cast<volatile hcpp::u32*>(bar0 + offset) = value;
        }

        // Acknowledges all pending causes; reading ICR also deasserts
        // the level-triggered INTx line.
        void irq_handler(void*, hcpp::u32) noexcept {
            (void)mmio_read(reg_icr);
            ++stat_irqs;
        }

        // EEPROM word read via EERD; only used when RAH0.AV is clear.
        hcpp::u16 eerd_read(hcpp::u8 word) noexcept {
            mmio_write(reg_eerd, eerd_start | (hcpp::u32(word) << 8));
            for (hcpp::u32 i = 0; i < spin_busy; ++i) {
                const hcpp::u32 v = mmio_read(reg_eerd);
                if ((v & eerd_done) != 0)
                    return static_cast<hcpp::u16>(v >> 16);
            }
            return 0;
        }

        void read_mac() noexcept {
            const hcpp::u32 lo = mmio_read(reg_ral0);
            const hcpp::u32 hi = mmio_read(reg_rah0);
            if ((hi & rah_av) != 0) {
                mac[0] = static_cast<hcpp::u8>(lo);
                mac[1] = static_cast<hcpp::u8>(lo >> 8);
                mac[2] = static_cast<hcpp::u8>(lo >> 16);
                mac[3] = static_cast<hcpp::u8>(lo >> 24);
                mac[4] = static_cast<hcpp::u8>(hi);
                mac[5] = static_cast<hcpp::u8>(hi >> 8);
                return;
            }
            for (hcpp::u8 i = 0; i < 3; ++i) {
                const hcpp::u16 w = eerd_read(i);
                mac[i * 2] = static_cast<hcpp::u8>(w);
                mac[i * 2 + 1] = static_cast<hcpp::u8>(w >> 8);
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

        void rings_setup() noexcept {
            // One page each holds 32 legacy 16-byte descriptors.
            for (hcpp::u16 i = 0; i < rx_desc_count; ++i) {
                rx_descs[i].addr = rx_pool + hcpp::u64(i) * buffer_size;
                rx_descs[i].status = 0;
            }
            for (hcpp::u16 i = 0; i < tx_desc_count; ++i) {
                // DD pre-set marks every TX slot "done/free" so the
                // first send() does not wait on a stale status.
                tx_descs[i].status = tx_status_dd;
                tx_descs[i].addr = tx_pool + hcpp::u64(i) * buffer_size;
            }
            rx_cursor = 0;

            mmio_write(reg_rdbal, static_cast<hcpp::u32>(rx_ring_phys));
            mmio_write(reg_rdbah, static_cast<hcpp::u32>(rx_ring_phys >> 32));
            mmio_write(reg_rdlen, rx_desc_count * 16);
            mmio_write(reg_rdh, 0);
            // Tail points one past the last armed descriptor; with a
            // full ring that is index count-1 (one spare slot stays
            // software-owned, the classic head==tail ambiguity).
            mmio_write(reg_rdt, rx_desc_count - 1);
            mmio_write(reg_rctl, rctl_en | rctl_sbp | rctl_upe | rctl_mpe |
                                     rctl_bam | rctl_secrc);

            mmio_write(reg_tdbal, static_cast<hcpp::u32>(tx_ring_phys));
            mmio_write(reg_tdbah, static_cast<hcpp::u32>(tx_ring_phys >> 32));
            mmio_write(reg_tdlen, tx_desc_count * 16);
            mmio_write(reg_tdh, 0);
            mmio_write(reg_tdt, 0);
            mmio_write(reg_tipg, tipg_82540);
            mmio_write(reg_tctl, tctl_en | tctl_psp | tctl_ct | tctl_cold);
        }

    }  // namespace

    bool e1000_probe() noexcept {
        present = false;
        irq_armed = false;
        stat_rx = 0;
        stat_tx = 0;
        stat_irqs = 0;
        const pci::PciDevice* dev = nullptr;
        for (hcpp::size_t i = 0; i < sizeof(e1000_device_ids) /
                                         sizeof(e1000_device_ids[0]);
             ++i) {
            dev = pci::find(intel_vendor, e1000_device_ids[i]);
            if (dev != nullptr) break;
        }
        if (dev == nullptr) {
            serial::write_line("e1000 dev=absent");
            return false;
        }

        // BAR0 is the 128 KiB MMIO window; identity-mapped like every
        // other driver in the tree, so it must sit below 4 GiB.
        const hcpp::u32 bar_low = pci::bar(*dev, 0);
        const hcpp::u32 bar_high = pci::bar(*dev, 1);
        const bool wide = (bar_low & 0x6) == 0x4;
        const hcpp::u64 base =
            pci::bar_memory_address(bar_low, wide ? bar_high : 0);
        if (!pci::bar_is_memory(bar_low) || base == 0 ||
            base >= 0x100000000ull || !memory::pmm_initialized()) {
            serial::write_line("e1000 dev=absent");
            return false;
        }
        pci::pci_enable_mem(*dev);
        pci::pci_enable_busmaster(*dev);
        bar0 = reinterpret_cast<volatile hcpp::u8*>(base);

        // Mask everything, then a full CTRL.RST; the bit self-clears.
        mmio_write(reg_imc, 0xFFFFFFFFu);
        mmio_write(reg_ctrl, mmio_read(reg_ctrl) | ctrl_rst);
        for (hcpp::u32 i = 0;
             i < spin_busy && (mmio_read(reg_ctrl) & ctrl_rst) != 0; ++i) {
        }
        // ~10 ms settle per the 82540 spec (PIT runs at 100 Hz).
        interrupts::pit_wait_ticks(1);

        read_mac();
        mmio_write(reg_ctrl, mmio_read(reg_ctrl) | ctrl_slu);
        // Drop the multicast table; promiscuous flags cover the rest.
        for (hcpp::u16 i = 0; i < 128; ++i)
            mmio_write(reg_mta + i * 4, 0);

        const hcpp::size_t rx_pages =
            (hcpp::size_t(rx_desc_count) * buffer_size + memory::page_size - 1) /
            memory::page_size;
        const hcpp::size_t tx_pages =
            (hcpp::size_t(tx_desc_count) * buffer_size + memory::page_size - 1) /
            memory::page_size;
        rx_ring_phys = memory::pmm_allocate_page();
        tx_ring_phys = memory::pmm_allocate_page();
        rx_pool = memory::pmm_allocate_pages(rx_pages);
        tx_pool = memory::pmm_allocate_pages(tx_pages);
        if (rx_ring_phys == 0 || tx_ring_phys == 0 || rx_pool == 0 ||
            tx_pool == 0) {
            serial::write_line("e1000 dev=fail");
            return false;
        }
        rx_descs = reinterpret_cast<volatile E1000RxDesc*>(rx_ring_phys);
        tx_descs = reinterpret_cast<volatile E1000TxDesc*>(tx_ring_phys);
        hcpp::set(reinterpret_cast<void*>(rx_ring_phys), 0, memory::page_size);
        hcpp::set(reinterpret_cast<void*>(tx_ring_phys), 0, memory::page_size);
        rings_setup();

        // Interrupt: INTx pin via pci_irq_install (MSI-X/MSI don't
        // exist on the 82540EM, so this lands on the legacy path and
        // the handler acks via ICR). Delivery stays poll-driven; when
        // no line can be armed the IMS mask simply stays at 0.
        if (pci_irq_install(*dev, irq_handler, nullptr)) {
            irq_armed = true;
            mmio_write(reg_ims, icr_txdw | icr_lsc | icr_rxdmt0 | icr_rxo |
                                    icr_rxt0);
        } else {
            mmio_write(reg_ims, 0);
        }
        (void)mmio_read(reg_icr);  // clear any stale cause bits
        present = true;

        serial::write_line("e1000 dev=ok");
        serial::write("e1000 mac=");
        write_mac();
        serial::write("\r\n");
        serial::write("e1000 link=");
        serial::write_line((mmio_read(reg_status) & status_lu) != 0
                               ? "up"
                               : "down");
        serial::write("e1000 irq=");
        serial::write_line(irq_armed ? "intx" : "poll");
        return true;
    }

    bool e1000_ready() noexcept { return present; }

    const hcpp::u8* e1000_mac() noexcept { return mac; }

    bool e1000_send(const void* frame, hcpp::size_t length) noexcept {
        if (!present || frame == nullptr || length == 0 ||
            length > buffer_size)
            return false;
        const hcpp::u16 tail =
            static_cast<hcpp::u16>(mmio_read(reg_tdt) % tx_desc_count);
        volatile E1000TxDesc* d = &tx_descs[tail];
        // Bounded wait for the slot to drain; with 32 slots and a
        // synchronous caller it is almost always already done.
        for (hcpp::u32 i = 0;
             i < spin_busy && (d->status & tx_status_dd) == 0; ++i) {
        }
        if ((d->status & tx_status_dd) == 0) return false;
        hcpp::u8* buf =
            reinterpret_cast<hcpp::u8*>(tx_pool + hcpp::u64(tail) * buffer_size);
        hcpp::copy(buf, frame, length);
        d->addr = tx_pool + hcpp::u64(tail) * buffer_size;
        d->length = static_cast<hcpp::u16>(length);
        d->cso = 0;
        d->cmd = tx_cmd_eop | tx_cmd_ifcs | tx_cmd_rs;
        d->status = 0;
        d->css = 0;
        d->special = 0;
        fence();
        mmio_write(reg_tdt, (tail + 1) % tx_desc_count);
        ++stat_tx;
        return true;
    }

    hcpp::size_t e1000_recv(void* frame, hcpp::size_t capacity) noexcept {
        if (!present || frame == nullptr) return 0;
        // Acks a pending INTx when the IRQ is armed; harmless in pure
        // poll mode and clears LSC/TXDW cause bits either way.
        (void)mmio_read(reg_icr);
        volatile E1000RxDesc* d = &rx_descs[rx_cursor];
        if ((d->status & rx_status_dd) == 0) return 0;
        hcpp::size_t length = d->length;
        if (length > capacity) length = capacity;
        hcpp::copy(frame,
                   reinterpret_cast<const hcpp::u8*>(
                       rx_pool + hcpp::u64(rx_cursor) * buffer_size),
                   length);
        d->status = 0;
        fence();
        // Hand the descriptor back: tail marks one-past-last-armed,
        // so writing the just-consumed index re-arms the ring.
        const hcpp::u16 old = rx_cursor;
        rx_cursor = static_cast<hcpp::u16>((rx_cursor + 1) % rx_desc_count);
        mmio_write(reg_rdt, old);
        ++stat_rx;
        return length;
    }

    void e1000_print_stats() noexcept {
        serial::write("e1000 rx=");
        serial::write_dec32(stat_rx);
        serial::write(" tx=");
        serial::write_dec32(stat_tx);
        serial::write(" irqs=");
        serial::write_dec32(stat_irqs);
        serial::write("\r\n");
    }

}  // namespace hype::net
