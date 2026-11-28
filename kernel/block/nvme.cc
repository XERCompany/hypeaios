#include "kernel/block/nvme.h"

#include "hcpp/memory.hpp"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"

namespace {

    // ---- Controller register offsets (NVMe 1.4 §3.1) ----
    constexpr hcpp::u32 reg_cap = 0x00;    // Controller Capabilities (u64)
    constexpr hcpp::u32 reg_intms = 0x0C;  // Interrupt Mask Set
    constexpr hcpp::u32 reg_cc = 0x14;     // Controller Configuration
    constexpr hcpp::u32 reg_csts = 0x1C;   // Controller Status
    constexpr hcpp::u32 reg_aqa = 0x24;    // Admin Queue Attributes
    constexpr hcpp::u32 reg_asq = 0x28;    // Admin SQ base (u64)
    constexpr hcpp::u32 reg_acq = 0x30;    // Admin CQ base (u64)
    constexpr hcpp::u32 reg_doorbells = 0x1000;

    constexpr hcpp::u32 cc_en = 1u << 0;
    constexpr hcpp::u32 cc_css_nvm = 0u << 0;   // NVM command set
    constexpr hcpp::u32 cc_mps_4k = 0u << 7;    // host page = 4 KiB
    constexpr hcpp::u32 cc_iosqes = 6u << 16;   // SQ entry = 64 B
    constexpr hcpp::u32 cc_iocqes = 4u << 20;   // CQ entry = 16 B
    constexpr hcpp::u32 csts_rdy = 1u << 0;
    constexpr hcpp::u32 csts_cfs = 1u << 1;

    // Admin queue opcodes (NVMe 1.4 §5).
    constexpr hcpp::u8 op_create_io_sq = 0x01;
    constexpr hcpp::u8 op_create_io_cq = 0x05;
    constexpr hcpp::u8 op_identify = 0x06;
    constexpr hcpp::u8 op_set_features = 0x09;
    // NVM command set opcodes (NVMe 1.4 §6).
    constexpr hcpp::u8 op_flush = 0x00;
    constexpr hcpp::u8 op_write = 0x01;
    constexpr hcpp::u8 op_read = 0x02;

    constexpr hcpp::u16 admin_entries = 32;   // SQ 2 KiB / CQ 512 B
    constexpr hcpp::u16 io_entries = 64;      // SQ 4 KiB / CQ 1 KiB
    constexpr hcpp::u16 io_qid = 1;
    constexpr hcpp::u32 fid_num_queues = 0x07;

    // One PRP-list page (512 entries) + PRP1 covers 513 pages, far
    // beyond the 64 KiB per-command transfer cap below.
    constexpr hcpp::u32 prp_list_entries = 512;
    constexpr hcpp::u32 max_cmd_bytes = 128 * 512;
    constexpr hcpp::u32 max_block_scratch = 4096;

    constexpr hcpp::u32 spin_ready = 0x2000000;   // RDY transitions
    constexpr hcpp::u32 spin_cmd = 0x20000000;    // completion polls

    // Submission queue entry: 16 dwords, 64 bytes.
    struct NvmeCommand {
        hcpp::u32 cdw[16];
    };
    static_assert(sizeof(NvmeCommand) == 64);

    // Completion queue entry: DW0 status-specific, DW1 reserved,
    // DW2 SQ head/id, DW3 = CID[15:0] | phase<<16 | status<<17.
    struct NvmeCompletion {
        hcpp::u32 dw0;
        hcpp::u32 dw1;
        hcpp::u16 sqhd;
        hcpp::u16 sqid;
        hcpp::u16 cid;
        hcpp::u16 status;    // bit0 = phase tag, bits15:1 = status
    };
    static_assert(sizeof(NvmeCompletion) == 16);

    struct Queue {
        volatile NvmeCommand* sq;
        volatile NvmeCompletion* cq;
        hcpp::uintptr_t sq_phys;
        hcpp::uintptr_t cq_phys;
        hcpp::u32 sq_db;     // MMIO offset of the SQ tail doorbell
        hcpp::u32 cq_db;     // MMIO offset of the CQ head doorbell
        hcpp::u16 entries;
        hcpp::u16 sq_tail;
        hcpp::u16 cq_head;
        hcpp::u16 next_cid;
        hcpp::u8 phase;      // expected completion phase tag
    };

    volatile hcpp::u8* nvme_bar;
    hcpp::u32 db_stride = 4;
    Queue admin_q;
    Queue io_q;
    hcpp::uintptr_t prp_list_phys;
    volatile hcpp::u64* prp_list;

    bool ready;
    hcpp::u32 active_nsid;
    hcpp::u32 ns_count;
    hcpp::u32 lba_size = 512;
    hcpp::u32 blocks_per_cmd = 128;   // native blocks per NVM cmd
    hcpp::u64 ns_blocks;
    hcpp::u8 ctrl_mdts;
    hcpp::u8 ctrl_mpsmin;
    char model[41];

    // DMA scratch. BSS is identity-mapped and physically contiguous;
    // page alignment keeps a 4 KiB transfer inside a single PRP.
    alignas(4096) hcpp::u8 ident_buf[4096];
    alignas(4096) hcpp::u8 bounce_buf[max_block_scratch];
    alignas(4096) hcpp::u8 verify_buf[max_block_scratch];

    hcpp::u32 mmio_read(hcpp::u32 offset) noexcept {
        return *reinterpret_cast<volatile hcpp::u32*>(nvme_bar + offset);
    }

    void mmio_write(hcpp::u32 offset, hcpp::u32 value) noexcept {
        *reinterpret_cast<volatile hcpp::u32*>(nvme_bar + offset) = value;
    }

    hcpp::u64 mmio_read64(hcpp::u32 offset) noexcept {
        const hcpp::u64 lo = mmio_read(offset);
        const hcpp::u64 hi = mmio_read(offset + 4);
        return (hi << 32) | lo;
    }

    void mmio_write64(hcpp::u32 offset, hcpp::u64 value) noexcept {
        mmio_write(offset, static_cast<hcpp::u32>(value & 0xFFFFFFFFull));
        mmio_write(offset + 4, static_cast<hcpp::u32>(value >> 32));
    }

    void pause() noexcept { asm volatile("pause"); }

    // Doorbell offset for queue `qid` (0-based): SQ tail at
    // 0x1000 + (2*qid)*stride, CQ head at 0x1000 + (2*qid+1)*stride.
    hcpp::u32 sq_doorbell(hcpp::u16 qid) noexcept {
        return reg_doorbells + 2u * qid * db_stride;
    }
    hcpp::u32 cq_doorbell(hcpp::u16 qid) noexcept {
        return reg_doorbells + (2u * qid + 1u) * db_stride;
    }

    // Submits one command on `q` and spins until its completion
    // appears (phase-bit toggle) or the budget expires. The command
    // is serialized per queue, so the first phase-matching entry is
    // ours. Returns false on timeout or non-zero status field.
    bool submit(Queue& q, const NvmeCommand& cmd,
                hcpp::u32* out_dw0 = nullptr) noexcept {
        const hcpp::u16 cid = q.next_cid++;
        volatile NvmeCommand* slot = &q.sq[q.sq_tail];
        for (hcpp::u32 i = 0; i < 16; ++i) slot->cdw[i] = cmd.cdw[i];
        slot->cdw[0] = cmd.cdw[0] | (static_cast<hcpp::u32>(cid) << 16);
        q.sq_tail = static_cast<hcpp::u16>((q.sq_tail + 1) % q.entries);
        mmio_write(q.sq_db, q.sq_tail);

        for (hcpp::u32 i = 0; i < spin_cmd; ++i) {
            volatile NvmeCompletion* c = &q.cq[q.cq_head];
            const hcpp::u16 st = c->status;
            if ((st & 1u) != q.phase) {
                if ((i & 0xFFFF) == 0) pause();
                continue;
            }
            const hcpp::u16 got_cid = c->cid;
            const hcpp::u32 dw0 = c->dw0;
            const bool ok = (st & 0xFFFEu) == 0;  // status field clear
            q.cq_head = static_cast<hcpp::u16>((q.cq_head + 1) % q.entries);
            if (q.cq_head == 0) q.phase ^= 1;    // wrapped: phase flips
            mmio_write(q.cq_db, q.cq_head);
            if (got_cid == cid && out_dw0 != nullptr) *out_dw0 = dw0;
            return ok && got_cid == cid;
        }
        return false;
    }

    bool queue_alloc(Queue& q, hcpp::u16 qid, hcpp::u16 entries) noexcept {
        q.sq_phys = hype::memory::pmm_allocate_page();
        q.cq_phys = hype::memory::pmm_allocate_page();
        if (q.sq_phys == 0 || q.cq_phys == 0) {
            if (q.sq_phys != 0) hype::memory::pmm_free_page(q.sq_phys);
            if (q.cq_phys != 0) hype::memory::pmm_free_page(q.cq_phys);
            q.sq_phys = q.cq_phys = 0;
            return false;
        }
        hcpp::set(reinterpret_cast<hcpp::u8*>(q.sq_phys), 0,
                  hype::memory::page_size);
        hcpp::set(reinterpret_cast<hcpp::u8*>(q.cq_phys), 0,
                  hype::memory::page_size);
        q.sq = reinterpret_cast<volatile NvmeCommand*>(q.sq_phys);
        q.cq = reinterpret_cast<volatile NvmeCompletion*>(q.cq_phys);
        q.sq_db = sq_doorbell(qid);
        q.cq_db = cq_doorbell(qid);
        q.entries = entries;
        q.sq_tail = 0;
        q.cq_head = 0;
        q.next_cid = 1;
        q.phase = 1;
        return true;
    }

    // One admin-queue command with `addr` as a single-PRP data page.
    bool admin_data(hcpp::u8 opcode, hcpp::u32 nsid, hcpp::u32 cdw10,
                    hcpp::uintptr_t addr,
                    hcpp::u32* out_dw0 = nullptr) noexcept {
        NvmeCommand cmd = {};
        cmd.cdw[0] = opcode;
        cmd.cdw[1] = nsid;
        cmd.cdw[6] = static_cast<hcpp::u32>(addr & 0xFFFFFFFFull);
        cmd.cdw[7] = static_cast<hcpp::u32>(addr >> 32);
        cmd.cdw[10] = cdw10;
        return submit(admin_q, cmd, out_dw0);
    }

    // Fills PRP1/PRP2 (cdw6..cdw9) for `bytes` at physical `addr`.
    // One page -> PRP1 only; two -> PRP1+PRP2; more -> PRP2 names a
    // PRP list in the dedicated PMM page.
    bool set_prps(NvmeCommand& cmd, hcpp::uintptr_t addr,
                  hcpp::u32 bytes) noexcept {
        const hcpp::u64 page = hype::memory::page_size;
        const hcpp::u32 pages = static_cast<hcpp::u32>(
            ((addr & (page - 1)) + bytes + page - 1) / page);
        cmd.cdw[6] = static_cast<hcpp::u32>(addr & 0xFFFFFFFFull);
        cmd.cdw[7] = static_cast<hcpp::u32>(addr >> 32);
        if (pages <= 1) return true;
        const hcpp::u64 second = (addr + page) & ~(page - 1);
        if (pages == 2) {
            cmd.cdw[8] = static_cast<hcpp::u32>(second & 0xFFFFFFFFull);
            cmd.cdw[9] = static_cast<hcpp::u32>(second >> 32);
            return true;
        }
        if (pages - 1 > prp_list_entries || prp_list == nullptr) {
            return false;
        }
        for (hcpp::u32 i = 0; i < pages - 1; ++i) {
            prp_list[i] = second + i * page;
        }
        cmd.cdw[8] = static_cast<hcpp::u32>(prp_list_phys & 0xFFFFFFFFull);
        cmd.cdw[9] = static_cast<hcpp::u32>(prp_list_phys >> 32);
        return true;
    }

    // NVM read/write/flush on I/O queue 1. `lba`/`blocks` are in the
    // namespace's native block size; `buffer` is an identity-mapped
    // physical address holding `blocks * lba_size` bytes (unused for
    // flush).
    bool io_cmd(hcpp::u8 opcode, hcpp::u64 lba, hcpp::u32 blocks,
                hcpp::uintptr_t buffer) noexcept {
        NvmeCommand cmd = {};
        cmd.cdw[0] = opcode;
        cmd.cdw[1] = active_nsid;
        if (opcode != op_flush) {
            if (!set_prps(cmd, buffer, blocks * lba_size)) return false;
            cmd.cdw[10] = static_cast<hcpp::u32>(lba & 0xFFFFFFFFull);
            cmd.cdw[11] = static_cast<hcpp::u32>(lba >> 32);
            cmd.cdw[12] = blocks - 1;   // 0-based NLB
        }
        return submit(io_q, cmd);
    }

    hcpp::u32 sectors_per_block() noexcept { return lba_size / 512; }

    void clean_ascii(char* out, const hcpp::u8* in,
                     hcpp::u32 len) noexcept {
        hcpp::u32 n = 0;
        for (hcpp::u32 i = 0; i < len; ++i) {
            const unsigned char c = in[i];
            out[n++] = (c >= 0x20 && c <= 0x7E)
                           ? static_cast<char>(c)
                           : ' ';
        }
        while (n != 0 && out[n - 1] == ' ') --n;
        out[n] = '\0';
    }

    bool dma_ok(hcpp::uintptr_t address, hcpp::u64 bytes) noexcept {
        return bytes != 0 && address + bytes <= 0x100000000ull &&
               address + bytes > address;
    }

    // IDENTIFY (0x06) with `cns` into the page-aligned ident_buf.
    bool identify(hcpp::u32 nsid, hcpp::u32 cns) noexcept {
        hcpp::set(ident_buf, 0, sizeof(ident_buf));
        return admin_data(op_identify, nsid, cns,
                          reinterpret_cast<hcpp::uintptr_t>(ident_buf));
    }

    bool identify_controller() noexcept {
        if (!identify(0, 1)) return false;   // CNS=1: controller
        clean_ascii(model, ident_buf + 24, 40);  // MN [63:24]
        ctrl_mdts = ident_buf[77];
        const hcpp::u8 sqes = ident_buf[512];    // required SQ size
        const hcpp::u8 cqes = ident_buf[513];
        // Sanity: we configured 64B SQ / 16B CQ entries in CC.
        if ((sqes & 0x0F) > 6 || (cqes & 0x0F) > 4) return false;
        ctrl_mpsmin =
            static_cast<hcpp::u8>((mmio_read64(reg_cap) >> 48) & 0xF);
        return true;
    }

    // CNS=2 active-namespace list -> pick and identify namespace 1
    // (or the first listed) so `lba_size`/`ns_blocks` are real.
    bool identify_namespace() noexcept {
        if (!identify(0, 2)) return false;   // CNS=2: active NSID list
        const volatile hcpp::u32* ids =
            reinterpret_cast<volatile hcpp::u32*>(ident_buf);
        hcpp::u32 first = 0;
        ns_count = 0;
        for (hcpp::u32 i = 0; i < 1024; ++i) {
            const hcpp::u32 id = ids[i];
            if (id == 0) break;
            ++ns_count;
            if (id == 1) first = 1;          // prefer NSID 1
            else if (first == 0) first = id;
        }
        if (first == 0) return false;
        active_nsid = first;

        if (!identify(active_nsid, 0)) return false;  // CNS=0: namespace
        hcpp::u64 ncap;
        hcpp::copy(&ncap, ident_buf + 8, 8);
        const hcpp::u8 flbas = ident_buf[26] & 0x0F;
        const hcpp::u8 lbads = ident_buf[128 + flbas * 4 + 2];
        if (lbads < 9 || lbads > 15) return false;    // 512B..32KiB
        lba_size = 1u << lbads;
        ns_blocks = ncap;
        if (ns_blocks == 0) return false;

        blocks_per_cmd = max_cmd_bytes / lba_size;
        if (blocks_per_cmd == 0) blocks_per_cmd = 1;
        // MDTS: max transfer = 2^MDTS * CAP.MPSMIN bytes (0 = no cap).
        if (ctrl_mdts != 0) {
            const hcpp::u64 cap_bytes =
                (1ull << ctrl_mdts) << (12 + ctrl_mpsmin);
            const hcpp::u64 cap_blocks = cap_bytes / lba_size;
            if (cap_blocks < blocks_per_cmd) {
                if (cap_blocks == 0) return false;
                blocks_per_cmd = static_cast<hcpp::u32>(cap_blocks);
            }
        }
        if (blocks_per_cmd > 65535) blocks_per_cmd = 65535;  // NLB is u16
        return true;
    }

    bool create_io_queues() noexcept {
        // Optional: request one queue pair via SET FEATURES (FID 7).
        NvmeCommand feat = {};
        feat.cdw[0] = op_set_features;
        feat.cdw[10] = fid_num_queues;
        feat.cdw[11] = 0;   // NSQR = NCQR = 0 (0-based: one pair)
        submit(admin_q, feat);   // best-effort, result ignored

        if (!queue_alloc(io_q, io_qid, io_entries)) return false;

        NvmeCommand cmd = {};
        cmd.cdw[0] = op_create_io_cq;
        cmd.cdw[6] = static_cast<hcpp::u32>(io_q.cq_phys & 0xFFFFFFFFull);
        cmd.cdw[7] = static_cast<hcpp::u32>(io_q.cq_phys >> 32);
        cmd.cdw[10] =
            (static_cast<hcpp::u32>(io_entries - 1) << 16) | io_qid;
        cmd.cdw[11] = 1;    // PC=1 (physically contiguous), IV=0
        if (!submit(admin_q, cmd)) return false;

        cmd = {};
        cmd.cdw[0] = op_create_io_sq;
        cmd.cdw[6] = static_cast<hcpp::u32>(io_q.sq_phys & 0xFFFFFFFFull);
        cmd.cdw[7] = static_cast<hcpp::u32>(io_q.sq_phys >> 32);
        cmd.cdw[10] =
            (static_cast<hcpp::u32>(io_entries - 1) << 16) | io_qid;
        cmd.cdw[11] = 1 | (static_cast<hcpp::u32>(io_qid) << 16);  // CQID
        return submit(admin_q, cmd);
    }

    // Verify read of LBA 0 for the "nvme rw=" marker: a live disk
    // returns at least one non-0x00 and one non-0xFF byte.
    bool verify_read() noexcept {
        hcpp::set(verify_buf, 0, sizeof(verify_buf));
        if (lba_size > sizeof(verify_buf) ||
            !hype::nvme::nvme_read_sectors(0, verify_buf,
                                           sectors_per_block())) {
            return false;
        }
        bool nonzero = false;
        bool not_ff = false;
        for (hcpp::u32 i = 0; i < lba_size; ++i) {
            if (verify_buf[i] != 0) nonzero = true;
            if (verify_buf[i] != 0xFF) not_ff = true;
        }
        return nonzero && not_ff;   // all-0x00 or all-0xFF = dead read
    }

}

namespace hype::nvme {

    bool nvme_probe() noexcept {
        ready = false;
        nvme_bar = nullptr;
        prp_list = nullptr;
        prp_list_phys = 0;
        active_nsid = 0;
        ns_count = 0;
        lba_size = 512;
        ns_blocks = 0;
        model[0] = '\0';
        admin_q = {};
        io_q = {};

        const pci::PciDevice* dev = nullptr;
        for (hcpp::size_t i = 0; i < pci::device_count(); ++i) {
            const pci::PciDevice& d = pci::devices()[i];
            if (d.class_code == 0x01 && d.subclass == 0x08 &&
                d.program_interface == 0x02) {
                dev = &d;
                break;
            }
        }
        if (dev == nullptr) {
            serial::write_line("nvme dev=absent");
            return false;
        }

        // BAR0 must be a 64-bit memory BAR below 4 GiB so the
        // identity-mapped MMIO view reaches it.
        const hcpp::u32 bar0 = pci::bar(*dev, 0);
        const hcpp::u32 bar1 = pci::bar(*dev, 1);
        const bool wide = (bar0 & 0x6) == 0x4;
        const hcpp::u64 base =
            pci::bar_memory_address(bar0, wide ? bar1 : 0);
        if (!pci::bar_is_memory(bar0) || !wide || base == 0 ||
            base >= 0x100000000ull || !memory::pmm_initialized()) {
            serial::write_line("nvme dev=absent");
            return false;
        }
        pci::pci_enable_mem(*dev);
        pci::pci_enable_busmaster(*dev);
        nvme_bar = reinterpret_cast<volatile hcpp::u8*>(base);

        // Polling driver: mask every interrupt vector up front.
        mmio_write(reg_intms, 0xFFFFFFFFu);

        const hcpp::u64 cap = mmio_read64(reg_cap);
        db_stride = 4u << ((cap >> 32) & 0xF);      // CAP.DSTRD
        const hcpp::u32 mqes =
            static_cast<hcpp::u32>(cap & 0xFFFF) + 1;  // 0-based

        // Controller disable: CC.EN=0 then wait CSTS.RDY=0.
        hcpp::u32 cc = mmio_read(reg_cc);
        if ((cc & cc_en) != 0) {
            mmio_write(reg_cc, cc & ~cc_en);
            hcpp::u32 i = 0;
            while ((mmio_read(reg_csts) & csts_rdy) != 0 &&
                   i < spin_ready) {
                ++i;
            }
            if ((mmio_read(reg_csts) & csts_rdy) != 0) {
                serial::write_line("nvme dev=fail");
                return false;
            }
        }

        // Admin queue pair on PMM pages (clamped to CAP.MQES).
        hcpp::u16 entries = admin_entries;
        if (entries > mqes) entries = static_cast<hcpp::u16>(mqes);
        if (entries < 4 || !queue_alloc(admin_q, 0, entries)) {
            serial::write_line("nvme dev=fail");
            return false;
        }
        prp_list_phys = memory::pmm_allocate_page();
        if (prp_list_phys == 0) {
            serial::write_line("nvme dev=fail");
            return false;
        }
        prp_list = reinterpret_cast<volatile hcpp::u64*>(prp_list_phys);

        mmio_write(reg_aqa,
                   (static_cast<hcpp::u32>(entries - 1) << 16) |
                       (entries - 1));
        mmio_write64(reg_asq, admin_q.sq_phys);
        mmio_write64(reg_acq, admin_q.cq_phys);
        mmio_write(reg_cc,
                   cc_en | cc_css_nvm | cc_mps_4k | cc_iosqes | cc_iocqes);

        hcpp::u32 i = 0;
        while ((mmio_read(reg_csts) & csts_rdy) == 0 && i < spin_ready) {
            if ((mmio_read(reg_csts) & csts_cfs) != 0) break;
            ++i;
        }
        if ((mmio_read(reg_csts) & (csts_rdy | csts_cfs)) != csts_rdy) {
            serial::write_line("nvme dev=fail");
            return false;
        }

        if (!identify_controller() || !identify_namespace() ||
            !create_io_queues()) {
            serial::write_line("nvme dev=fail");
            return false;
        }

        serial::write("nvme dev=ok ns=");
        serial::write_dec32(ns_count);
        serial::write(" lba=");
        serial::write_dec32(lba_size);
        serial::write("\r\n");

        serial::write("nvme ns");
        serial::write_dec32(active_nsid);
        serial::write(" cap=");
        serial::write_dec32(static_cast<hcpp::u32>(
            (ns_blocks * lba_size) / (1024 * 1024)));
        serial::write("MB model=");
        serial::write_line(model);

        ready = true;
        serial::write("nvme rw=");
        serial::write_line(verify_read() ? "ok" : "fail");
        return true;
    }

    bool nvme_ready() noexcept { return ready; }

    hcpp::u32 nvme_namespace_count() noexcept { return ns_count; }

    hcpp::u32 nvme_sector_size() noexcept { return lba_size; }

    bool nvme_identify(hype::ata::AtaInfo& out) noexcept {
        if (!ready) return false;
        out = {};
        out.lba48 = true;
        out.sectors = ns_blocks * sectors_per_block();
        for (hcpp::u32 i = 0; i < 41; ++i) out.model[i] = model[i];
        return true;
    }

    bool nvme_read_sectors(hcpp::u64 lba, void* buffer,
                           hcpp::u32 sectors) noexcept {
        if (!ready || buffer == nullptr || sectors == 0 ||
            (lba >> 48) != 0) {
            return false;
        }
        const hcpp::u32 spb = sectors_per_block();
        hcpp::u8* out = static_cast<hcpp::u8*>(buffer);
        while (sectors != 0) {
            const hcpp::u64 block = lba / spb;
            const hcpp::u32 off = static_cast<hcpp::u32>(lba % spb);
            if (off == 0 && sectors >= spb) {
                // Aligned fast path: whole native blocks straight into
                // the caller's buffer via PRP chain.
                hcpp::u32 n = sectors / spb;
                if (n > blocks_per_cmd) n = blocks_per_cmd;
                const hcpp::uintptr_t phys =
                    reinterpret_cast<hcpp::uintptr_t>(out);
                if (!dma_ok(phys, static_cast<hcpp::u64>(n) * lba_size) ||
                    !io_cmd(op_read, block, n, phys)) {
                    return false;
                }
                lba += static_cast<hcpp::u64>(n) * spb;
                out += static_cast<hcpp::u64>(n) * lba_size;
                sectors -= n * spb;
            } else {
                // Sub-block request on a >512B device: bounce one
                // native block and copy the slice the caller wants.
                if (lba_size > sizeof(bounce_buf)) return false;
                const hcpp::uintptr_t phys =
                    reinterpret_cast<hcpp::uintptr_t>(bounce_buf);
                if (!io_cmd(op_read, block, 1, phys)) return false;
                hcpp::u32 n = spb - off;
                if (n > sectors) n = sectors;
                hcpp::copy(out, bounce_buf + off * 512, n * 512);
                lba += n;
                out += static_cast<hcpp::u64>(n) * 512;
                sectors -= n;
            }
        }
        return true;
    }

    bool nvme_write_sectors(hcpp::u64 lba, const void* buffer,
                            hcpp::u32 sectors) noexcept {
        if (!ready || buffer == nullptr || sectors == 0 ||
            (lba >> 48) != 0) {
            return false;
        }
        const hcpp::u32 spb = sectors_per_block();
        // Writes must land on whole native blocks (no RMW in v1).
        if ((lba % spb) != 0 || (sectors % spb) != 0) return false;
        const hcpp::u8* in = static_cast<const hcpp::u8*>(buffer);
        hcpp::u64 block = lba / spb;
        while (sectors != 0) {
            hcpp::u32 n = sectors / spb;
            if (n > blocks_per_cmd) n = blocks_per_cmd;
            const hcpp::uintptr_t phys =
                reinterpret_cast<hcpp::uintptr_t>(in);
            if (!dma_ok(phys, static_cast<hcpp::u64>(n) * lba_size) ||
                !io_cmd(op_write, block, n, phys)) {
                return false;
            }
            block += n;
            in += static_cast<hcpp::u64>(n) * lba_size;
            sectors -= n * spb;
        }
        return true;
    }

    bool nvme_flush() noexcept {
        if (!ready) return false;
        return io_cmd(op_flush, 0, 0, 0);
    }

}
