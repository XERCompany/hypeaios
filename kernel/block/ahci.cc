#include "kernel/block/ahci.h"

#include "hcpp/memory.hpp"
#include "kernel/ata.h"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace {

    // ---- HBA register offsets (AHCI 1.3 §3.1 / §3.3) ----
    constexpr hcpp::u32 hba_ghc = 0x04;    // Global Host Control
    constexpr hcpp::u32 hba_pi = 0x0C;     // Ports Implemented
    constexpr hcpp::u32 hba_cap2 = 0x24;   // Host Capabilities Extended
    constexpr hcpp::u32 hba_bohc = 0x28;   // BIOS/OS Handoff Control

    constexpr hcpp::u32 ghc_ae = 1u << 31;  // AHCI Enable
    constexpr hcpp::u32 ghc_hr = 1u << 0;   // HBA Reset
    constexpr hcpp::u32 cap2_boh = 1u << 0; // BIOS/OS handoff supported
    constexpr hcpp::u32 bohc_bos = 1u << 0; // BIOS Owned Semaphore
    constexpr hcpp::u32 bohc_oos = 1u << 1; // OS Owned Semaphore
    constexpr hcpp::u32 bohc_bb = 1u << 4;  // BIOS Busy (W1C)

    // Port registers: base = 0x100 + port * 0x80.
    constexpr hcpp::u32 port_base = 0x100;
    constexpr hcpp::u32 port_stride = 0x80;
    constexpr hcpp::u32 px_clb = 0x00;   // Command List Base
    constexpr hcpp::u32 px_clbu = 0x04;
    constexpr hcpp::u32 px_fb = 0x08;    // Received FIS Base
    constexpr hcpp::u32 px_fbu = 0x0C;
    constexpr hcpp::u32 px_is = 0x10;    // Interrupt Status (W1C)
    constexpr hcpp::u32 px_cmd = 0x18;   // Command and Status
    constexpr hcpp::u32 px_tfd = 0x20;   // Task File Data
    constexpr hcpp::u32 px_ssts = 0x28;  // SATA Status
    constexpr hcpp::u32 px_sctl = 0x2C;  // SATA Control
    constexpr hcpp::u32 px_serr = 0x30;  // SATA Error (W1C)
    constexpr hcpp::u32 px_ci = 0x38;    // Command Issue

    constexpr hcpp::u32 px_cmd_st = 1u << 0;    // Start
    constexpr hcpp::u32 px_cmd_sud = 1u << 1;   // Spin-Up Device
    constexpr hcpp::u32 px_cmd_pod = 1u << 2;   // Power On Device
    constexpr hcpp::u32 px_cmd_fre = 1u << 4;   // FIS Receive Enable
    constexpr hcpp::u32 px_cmd_fr = 1u << 14;   // FIS Receive Running
    constexpr hcpp::u32 px_cmd_cr = 1u << 15;   // Command List Running

    constexpr hcpp::u32 px_tfd_err = 0x01;
    constexpr hcpp::u32 px_tfd_drq = 0x08;
    constexpr hcpp::u32 px_tfd_bsy = 0x80;
    constexpr hcpp::u32 px_is_tfes = 1u << 30;  // Task File Error Status
    constexpr hcpp::u32 sctl_det_comreset = 0x1;

    constexpr hcpp::u8 fis_reg_h2d = 0x27;
    constexpr hcpp::u8 ata_identify = 0xEC;
    constexpr hcpp::u8 ata_read_dma_ext = 0x25;
    constexpr hcpp::u8 ata_write_dma_ext = 0x35;
    constexpr hcpp::u8 ata_flush_ext = 0xEA;
    constexpr hcpp::u8 ata_dev_lba = 0x40;  // device reg: LBA addressing

    constexpr hcpp::u32 max_ports = 32;
    constexpr hcpp::u32 cmd_slots = 32;    // command list entries per port
    constexpr hcpp::u32 prdt_max = 8;      // PRDT entries per command table
    constexpr hcpp::u32 prdt_span = 0x400000;  // 4 MiB cap per PRDT entry

    // Bounded spin budgets. Command completion gets the largest: a real
    // disk flush can legitimately take seconds of MMIO poll time.
    constexpr hcpp::u32 spin_idle = 0x200000;     // BSY/DRQ drain
    constexpr hcpp::u32 spin_cmd = 0x20000000;    // command completion
    constexpr hcpp::u32 spin_link = 0x1000000;    // DET=3 link wait
    constexpr hcpp::u32 spin_handoff = 0x400000;  // BIOS semaphore release
    constexpr hcpp::u32 spin_reset = 0x200000;    // HBA HR / COMRESET
    constexpr hcpp::u32 delay_reset_ms = 0x100000;  // ~ms-scale pause loop

    // HBA command header (command list entry), 32 bytes, 1KB-aligned list.
    struct CmdHeader {
        hcpp::u16 flags;   // [4:0] CFL(dwords), [5] ATAPI, [6] W, [10] C
        hcpp::u16 prdtl;   // PRDT entry count in the command table
        hcpp::u32 prdbc;   // bytes transferred so far (HBA updates)
        hcpp::u32 ctba;    // command table base, 128B-aligned
        hcpp::u32 ctbau;
        hcpp::u32 rsv[4];
    };
    static_assert(sizeof(CmdHeader) == 32);

    struct PrdtEntry {
        hcpp::u32 dba;    // data base address, word-aligned
        hcpp::u32 dbau;
        hcpp::u32 rsv;
        hcpp::u32 dbc;    // [21:0] byte count - 1, [31] IOC
    };
    static_assert(sizeof(PrdtEntry) == 16);

    struct CmdTable {
        hcpp::u8 cfis[64];    // command FIS
        hcpp::u8 acmd[16];    // ATAPI command (unused)
        hcpp::u8 rsv[48];
        PrdtEntry prdt[prdt_max];
    };
    static_assert(sizeof(CmdTable) == 256);

    constexpr hcpp::size_t table_bytes =
        cmd_slots * sizeof(CmdTable);  // 8 KiB -> 2 pages

    volatile hcpp::u8* abar;
    hcpp::u8 live_ports;
    hcpp::u8 active_port;
    bool ready;

    struct PortState {
        hcpp::uintptr_t clb;
        hcpp::uintptr_t fb;
        hcpp::uintptr_t tables;
        bool live;
        bool disk;
        hype::ata::AtaInfo info;
    };
    PortState ports[max_ports];

    // Static DMA scratch for IDENTIFY and the rw=ok verify read; BSS is
    // identity-mapped and physically contiguous, so it is DMA-safe.
    alignas(256) hcpp::u8 ident_buf[512];
    alignas(256) hcpp::u8 verify_buf[512];
    alignas(256) hcpp::u8 verify_ref[512];

    hcpp::u32 mmio_read(hcpp::u32 offset) noexcept {
        return *reinterpret_cast<volatile hcpp::u32*>(abar + offset);
    }

    void mmio_write(hcpp::u32 offset, hcpp::u32 value) noexcept {
        *reinterpret_cast<volatile hcpp::u32*>(abar + offset) = value;
    }

    hcpp::u32 port_off(hcpp::u8 port) noexcept {
        return port_base + static_cast<hcpp::u32>(port) * port_stride;
    }

    hcpp::u32 px_read(hcpp::u8 port, hcpp::u32 reg) noexcept {
        return mmio_read(port_off(port) + reg);
    }

    void px_write(hcpp::u8 port, hcpp::u32 reg, hcpp::u32 value) noexcept {
        mmio_write(port_off(port) + reg, value);
    }

    void pause() noexcept { asm volatile("pause"); }

    void short_delay() noexcept {
        for (hcpp::u32 i = 0; i < delay_reset_ms; ++i) pause();
    }

    // Wait until the port's command engine is idle: BSY and DRQ clear.
    bool wait_idle(hcpp::u8 port, hcpp::u32 limit) noexcept {
        for (hcpp::u32 i = 0; i < limit; ++i) {
            const hcpp::u32 tfd = px_read(port, px_tfd);
            if ((tfd & (px_tfd_bsy | px_tfd_drq)) == 0) return true;
        }
        return false;
    }

    bool wait_link(hcpp::u8 port, hcpp::u32 limit) noexcept {
        for (hcpp::u32 i = 0; i < limit; ++i) {
            if ((px_read(port, px_ssts) & 0xF) == 3) return true;
        }
        return false;
    }

    // Stops command list and FIS engines; bounded waits for CR/FR.
    bool port_stop(hcpp::u8 port) noexcept {
        hcpp::u32 cmd = px_read(port, px_cmd);
        cmd &= ~(px_cmd_st | px_cmd_fre);
        px_write(port, px_cmd, cmd);
        for (hcpp::u32 i = 0; i < spin_idle; ++i) {
            const hcpp::u32 now = px_read(port, px_cmd);
            if ((now & (px_cmd_cr | px_cmd_fr)) == 0) return true;
        }
        return false;
    }

    // COMRESET via PxSCTL.DET: assert for ~1ms, release, wait for the
    // link to renegotiate (DET=3), then restart the engines.
    bool port_recover(hcpp::u8 port) noexcept {
        port_stop(port);
        const hcpp::u32 sctl = px_read(port, px_sctl);
        px_write(port, px_sctl, (sctl & ~0xFu) | sctl_det_comreset);
        short_delay();
        px_write(port, px_sctl, sctl & ~0xFu);
        if (!wait_link(port, spin_link)) return false;
        px_write(port, px_serr, 0xFFFFFFFFu);
        px_write(port, px_is, 0xFFFFFFFFu);
        px_write(port, px_cmd,
                 (px_read(port, px_cmd) | px_cmd_fre | px_cmd_st));
        return wait_idle(port, spin_idle);
    }

    // Issues one slot-0 register-H2D command. `data` is the physical
    // (== virtual, identity-mapped) buffer of `bytes`; zero for non-data
    // commands. `is_write` sets the H->D direction bit in the header.
    bool issue(hcpp::u8 port, hcpp::u8 command, hcpp::u64 lba,
               hcpp::u16 count, hcpp::uintptr_t data, hcpp::u32 bytes,
               bool is_write) noexcept {
        if (!wait_idle(port, spin_idle) && !port_recover(port)) {
            return false;
        }
        const PortState& state = ports[port];
        volatile CmdHeader* header =
            reinterpret_cast<volatile CmdHeader*>(state.clb);
        volatile CmdTable* table =
            reinterpret_cast<volatile CmdTable*>(state.tables);
        hcpp::u8* cfis = const_cast<hcpp::u8*>(table->cfis);
        hcpp::set(cfis, 0, 64);
        hcpp::set(const_cast<hcpp::u8*>(table->acmd), 0, 16);

        // Register H2D FIS, 20 bytes -> CFL = 5 dwords.
        cfis[0] = fis_reg_h2d;
        cfis[1] = 0x80;  // C=1 (update command reg), pmport = 0
        cfis[2] = command;
        cfis[3] = 0;     // features low
        cfis[4] = static_cast<hcpp::u8>(lba & 0xFF);
        cfis[5] = static_cast<hcpp::u8>((lba >> 8) & 0xFF);
        cfis[6] = static_cast<hcpp::u8>((lba >> 16) & 0xFF);
        cfis[7] = ata_dev_lba;
        cfis[8] = static_cast<hcpp::u8>((lba >> 24) & 0xFF);
        cfis[9] = static_cast<hcpp::u8>((lba >> 32) & 0xFF);
        cfis[10] = static_cast<hcpp::u8>((lba >> 40) & 0xFF);
        cfis[11] = 0;    // features high
        cfis[12] = static_cast<hcpp::u8>(count & 0xFF);
        cfis[13] = static_cast<hcpp::u8>((count >> 8) & 0xFF);
        cfis[14] = 0;    // icc
        cfis[15] = 0;    // control

        // PRDT: each entry covers at most 4 MiB and never crosses a
        // 4 MiB boundary.
        hcpp::u16 prdtl = 0;
        hcpp::u32 remaining = bytes;
        hcpp::uintptr_t cursor = data;
        while (remaining != 0 && prdtl < prdt_max) {
            hcpp::u32 chunk = remaining;
            const hcpp::u32 to_boundary =
                prdt_span - static_cast<hcpp::u32>(cursor % prdt_span);
            if (chunk > to_boundary) chunk = to_boundary;
            if (chunk > prdt_span) chunk = prdt_span;
            table->prdt[prdtl].dba =
                static_cast<hcpp::u32>(cursor & 0xFFFFFFFFull);
            table->prdt[prdtl].dbau =
                static_cast<hcpp::u32>(cursor >> 32);
            table->prdt[prdtl].rsv = 0;
            table->prdt[prdtl].dbc = chunk - 1;
            ++prdtl;
            cursor += chunk;
            remaining -= chunk;
        }
        if (remaining != 0) return false;  // buffer too large for table

        header->flags =
            static_cast<hcpp::u16>(5 | (is_write ? (1u << 6) : 0));
        header->prdtl = prdtl;
        header->prdbc = 0;
        header->ctba = static_cast<hcpp::u32>(state.tables & 0xFFFFFFFFull);
        header->ctbau = static_cast<hcpp::u32>(state.tables >> 32);

        px_write(port, px_is, 0xFFFFFFFFu);   // clear pending status
        px_write(port, px_ci, 1u << 0);       // issue slot 0

        bool done = false;
        bool error = false;
        for (hcpp::u32 i = 0; i < spin_cmd; ++i) {
            const hcpp::u32 is = px_read(port, px_is);
            if ((is & px_is_tfes) != 0) {
                error = true;
                break;
            }
            if ((px_read(port, px_ci) & 1) == 0) {
                done = true;
                break;
            }
        }
        if (!done || error) {
            port_recover(port);
            return false;
        }
        // TFES can arrive together with CI clearing on some HBAs.
        if ((px_read(port, px_is) & px_is_tfes) != 0 ||
            (px_read(port, px_tfd) & px_tfd_err) != 0) {
            return false;
        }
        return true;
    }

    bool dma_ok(hcpp::uintptr_t address, hcpp::u32 bytes) noexcept {
        return bytes != 0 && (address & 1) == 0 &&
               (address + bytes) <= 0x100000000ull;
    }

    bool port_identify(hcpp::u8 port) noexcept {
        hcpp::set(ident_buf, 0, sizeof(ident_buf));
        if (!issue(port, ata_identify, 0, 0,
                   reinterpret_cast<hcpp::uintptr_t>(ident_buf), 512,
                   false)) {
            return false;
        }
        hype::ata::AtaInfo& info = ports[port].info;
        info = {};
        for (hcpp::u32 i = 0; i < 40; i += 2) {
            info.model[i] = static_cast<char>(ident_buf[54 + i + 1]);
            info.model[i + 1] = static_cast<char>(ident_buf[54 + i]);
        }
        info.model[40] = '\0';
        for (hcpp::u32 i = 0; i < 40; ++i) {
            const unsigned char c =
                static_cast<unsigned char>(info.model[i]);
            if (c < 0x20 || c > 0x7E) info.model[i] = ' ';
        }
        for (hcpp::u32 i = 40; i != 0 && info.model[i - 1] == ' '; --i) {
            info.model[i - 1] = '\0';
        }
        auto le32 = [](const hcpp::u8* p) noexcept {
            return static_cast<hcpp::u32>(p[0]) |
                   (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        };
        info.lba48 = (ident_buf[167] & 0x04) != 0;
        const hcpp::u64 sectors48 =
            static_cast<hcpp::u64>(le32(ident_buf + 200)) |
            (static_cast<hcpp::u64>(le32(ident_buf + 204)) << 32);
        const hcpp::u32 sectors28 = le32(ident_buf + 120);
        info.sectors =
            (info.lba48 && sectors48 != 0) ? sectors48 : sectors28;
        return true;
    }

    // Brings up one implemented port: link check, engine stop, CLB/FB
    // buffers, spin-up, FIS receive, start. False keeps the port unused.
    bool port_start(hcpp::u8 port) noexcept {
        const hcpp::u32 ssts = px_read(port, px_ssts);
        if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) {
            return false;  // no device or link not active
        }
        if (!port_stop(port)) return false;

        PortState& state = ports[port];
        state.clb = hype::memory::pmm_allocate_page();
        state.fb = hype::memory::pmm_allocate_page();
        const hcpp::size_t table_pages =
            (table_bytes + hype::memory::page_size - 1) / hype::memory::page_size;
        state.tables = hype::memory::pmm_allocate_pages(table_pages);
        if (state.clb == 0 || state.fb == 0 || state.tables == 0) {
            if (state.clb != 0) hype::memory::pmm_free_page(state.clb);
            if (state.fb != 0) hype::memory::pmm_free_page(state.fb);
            if (state.tables != 0) {
                hype::memory::pmm_free_pages(state.tables, table_pages);
            }
            return false;
        }
        hcpp::set(reinterpret_cast<hcpp::u8*>(state.clb), 0,
                  hype::memory::page_size);
        hcpp::set(reinterpret_cast<hcpp::u8*>(state.fb), 0,
                  hype::memory::page_size);
        hcpp::set(reinterpret_cast<hcpp::u8*>(state.tables), 0,
                  table_pages * hype::memory::page_size);

        px_write(port, px_clb,
                 static_cast<hcpp::u32>(state.clb & 0xFFFFFFFFull));
        px_write(port, px_clbu, static_cast<hcpp::u32>(state.clb >> 32));
        px_write(port, px_fb,
                 static_cast<hcpp::u32>(state.fb & 0xFFFFFFFFull));
        px_write(port, px_fbu, static_cast<hcpp::u32>(state.fb >> 32));

        // Clear error/interrupt status (W1C), spin up + power on, enable
        // FIS receive, then start the command engine.
        px_write(port, px_serr, 0xFFFFFFFFu);
        px_write(port, px_is, 0xFFFFFFFFu);
        px_write(port, px_cmd,
                 px_read(port, px_cmd) | px_cmd_sud | px_cmd_pod);
        px_write(port, px_cmd, px_read(port, px_cmd) | px_cmd_fre);
        px_write(port, px_cmd, px_read(port, px_cmd) | px_cmd_st);

        state.live = wait_link(port, spin_link) &&
                     wait_idle(port, spin_idle);
        if (!state.live) port_stop(port);  // leave engines quiet
        return state.live;
    }

    // Verify read for the "ahci rw=" marker: sector 0 through the AHCI
    // path. When legacy ATA is also present, bytes must match the PIO
    // read; otherwise a non-zero content check is enough.
    bool verify_read() noexcept {
        hcpp::set(verify_buf, 0, sizeof(verify_buf));
        if (!hype::ahci::ahci_read(0, verify_buf, 1)) return false;
        if (hype::ata::ata_present() &&
            hype::ata::ata_pio_read(0, verify_ref, 1)) {
            return hcpp::compare(verify_buf, verify_ref, 512) == 0;
        }
        bool nonzero = false;
        bool not_ff = false;
        for (hcpp::u32 i = 0; i < 512; ++i) {
            if (verify_buf[i] != 0) nonzero = true;
            if (verify_buf[i] != 0xFF) not_ff = true;
        }
        return nonzero && not_ff;  // all-0x00 or all-0xFF = dead read
    }

}

namespace hype::ahci {

    bool ahci_probe() noexcept {
        ready = false;
        live_ports = 0;
        active_port = 0xFF;
        abar = nullptr;
        for (PortState& p : ports) p = {};

        const pci::PciDevice* dev = nullptr;
        for (hcpp::size_t i = 0; i < pci::device_count(); ++i) {
            const pci::PciDevice& d = pci::devices()[i];
            if (d.class_code == 0x01 && d.subclass == 0x06 &&
                d.program_interface == 0x01) {
                dev = &d;
                break;
            }
        }
        if (dev == nullptr) {
            serial::write_line("ahci dev=absent");
            return false;
        }
        const hcpp::u32 bar5 = pci::bar(*dev, 5);
        const hcpp::u64 abar_phys = pci::bar_memory_address(bar5);
        if (!pci::bar_is_memory(bar5) || abar_phys == 0 ||
            abar_phys >= 0x100000000ull || !hype::memory::pmm_initialized()) {
            serial::write_line("ahci dev=absent");
            return false;
        }
        pci::pci_enable_mem(*dev);
        pci::pci_enable_busmaster(*dev);
        abar = reinterpret_cast<volatile hcpp::u8*>(abar_phys);

        mmio_write(hba_ghc, mmio_read(hba_ghc) | ghc_ae);

        // BIOS/OS ownership handoff (AHCI 1.3 §10.6.3): only when the
        // controller declares support in CAP2.BOH.
        if ((mmio_read(hba_cap2) & cap2_boh) != 0) {
            hcpp::u32 bohc = mmio_read(hba_bohc);
            bohc |= bohc_oos;
            if ((bohc & bohc_bb) != 0) bohc |= bohc_bb;  // W1C
            mmio_write(hba_bohc, bohc);
            hcpp::u32 i = 0;
            while ((mmio_read(hba_bohc) & bohc_bos) != 0 &&
                   i < spin_handoff) {
                ++i;
            }
        }

        // Full HBA reset: HR is self-clearing; bounded wait, then AE
        // must be set again since reset drops it.
        mmio_write(hba_ghc, mmio_read(hba_ghc) | ghc_hr);
        hcpp::u32 reset_spin = 0;
        while ((mmio_read(hba_ghc) & ghc_hr) != 0 &&
               reset_spin < spin_reset) {
            ++reset_spin;
        }
        if ((mmio_read(hba_ghc) & ghc_hr) != 0) {
            serial::write_line("ahci dev=absent");
            return false;
        }
        mmio_write(hba_ghc, mmio_read(hba_ghc) | ghc_ae);

        const hcpp::u32 implemented = mmio_read(hba_pi);
        for (hcpp::u8 port = 0; port < max_ports; ++port) {
            if ((implemented & (1u << port)) == 0) continue;
            if (!port_start(port)) continue;
            ++live_ports;
        }
        serial::write("ahci dev=ok ports=");
        serial::write_dec32(live_ports);
        serial::write("\r\n");
        if (live_ports == 0) return false;

        // Identify every live port; the first real disk becomes the
        // block device.
        for (hcpp::u8 port = 0; port < max_ports; ++port) {
            if (!ports[port].live) continue;
            if (!port_identify(port)) continue;
            ports[port].disk = true;
            serial::write("ahci port");
            serial::write_dec32(port);
            serial::write(" model=");
            serial::write_line(ports[port].info.model);
            if (active_port == 0xFF) active_port = port;
        }
        if (active_port == 0xFF) return false;
        ready = true;
        serial::write("ahci rw=");
        serial::write_line(verify_read() ? "ok" : "fail");
        return true;
    }

    bool ahci_ready() noexcept { return ready; }

    hcpp::u8 ahci_port() noexcept { return active_port; }

    bool ahci_identify(hype::ata::AtaInfo& out) noexcept {
        if (!ready || active_port == 0xFF) return false;
        out = ports[active_port].info;
        return true;
    }

    bool ahci_read(hcpp::u64 lba, void* buffer, hcpp::u16 sectors) noexcept {
        if (!ready || buffer == nullptr || sectors == 0 ||
            (lba >> 48) != 0) {
            return false;
        }
        const hcpp::uintptr_t data =
            reinterpret_cast<hcpp::uintptr_t>(buffer);
        const hcpp::u32 bytes = static_cast<hcpp::u32>(sectors) * 512;
        if (!dma_ok(data, bytes)) return false;
        return issue(active_port, ata_read_dma_ext, lba, sectors, data,
                     bytes, false);
    }

    bool ahci_write(hcpp::u64 lba, const void* buffer,
                    hcpp::u16 sectors) noexcept {
        if (!ready || buffer == nullptr || sectors == 0 ||
            (lba >> 48) != 0) {
            return false;
        }
        const hcpp::uintptr_t data =
            reinterpret_cast<hcpp::uintptr_t>(buffer);
        const hcpp::u32 bytes = static_cast<hcpp::u32>(sectors) * 512;
        if (!dma_ok(data, bytes)) return false;
        return issue(active_port, ata_write_dma_ext, lba, sectors, data,
                     bytes, true);
    }

    bool ahci_flush() noexcept {
        if (!ready) return false;
        return issue(active_port, ata_flush_ext, 0, 0, 0, 0, false);
    }

}
