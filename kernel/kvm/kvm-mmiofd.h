#pragma once

#include "hcpp/types.hpp"

namespace hype::kvm {

    // MmioFd — a registered MMIO region, modelled on Linux KVM's
    // kvm_io_bus device table (the plumbing virtio-mmio devices plug
    // into). Each region describes a [gpa, gpa_end) guest-physical
    // window backed not by RAM but by a pair of read/write handlers —
    // typically a virtio-mmio device's register file.
    //
    // Routing: a guest access to an unmapped EPT GPA (or to a GPA the
    // EPT deliberately leaves not-present, which is how MMIO windows are
    // trapped) produces an EPT-violation VM exit whose handler calls
    // mmiofd_route_read()/mmiofd_route_write(). A hit invokes the
    // registered handler and the vcpu resumes; a miss means the access
    // was to genuinely unmapped space and the caller injects a guest
    // fault. This is exactly the path a virtio-mmio transport takes:
    // register one MmioFd per device covering its 512-byte register
    // window, back read_fn/write_fn by the virtqueue device model.
    //
    // `coalesced` marks regions eligible for a coalesced-MMIO ring
    // (Linux KVM coalesced_mmio): cheap posted writes can be batched
    // instead of exiting per store — a virtio-mmio consumer sets it on
    // the queue-notify window.
    //
    // Header-only, no heap: fixed slot table.

    constexpr hcpp::size_t mmiofd_max_slots = 16;
    constexpr hcpp::u16 mmiofd_invalid_slot = 0xFFFF;

    // MMIO access handlers. `size` is the access width in bytes
    // (1/2/4/8). read_fn returns the value the guest's destination
    // register should receive; write_fn consumes the guest's store.
    // `context` is the opaque device pointer passed at register time.
    using MmioReadFn = hcpp::u64 (*)(void *context, hcpp::u64 gpa,
                                   hcpp::u8 size) noexcept;
    using MmioWriteFn = void (*)(void *context, hcpp::u64 gpa,
                                 hcpp::u64 value, hcpp::u8 size) noexcept;

    struct MmioFd {
        hcpp::u64 gpa;        // first guest-physical byte of the window
        hcpp::u64 gpa_end;    // one past the last byte (exclusive end)
        MmioReadFn read_fn;   // guest-load handler (never nullptr once live)
        MmioWriteFn write_fn; // guest-store handler (never nullptr once live)
        void *context;        // device model state handed back to the handlers
        hcpp::u16 slot;       // index in the mmio bus table
        bool coalesced;       // eligible for a coalesced-mmio ring
        bool registered;      // visible to the exit router
    };

    namespace detail {
        inline MmioFd *mmiofd_table() noexcept {
            static MmioFd table[mmiofd_max_slots]{};
            return table;
        }
        inline hcpp::u32 &mmiofd_read_hits() noexcept {
            static hcpp::u32 hits = 0;
            return hits;
        }
        inline hcpp::u32 &mmiofd_write_hits() noexcept {
            static hcpp::u32 hits = 0;
            return hits;
        }
    }  // namespace detail

    // Registers an MMIO window [gpa, gpa+length). Fails (nullptr) when
    // the table is full, the range is empty, either handler is missing,
    // or the range overlaps an already-registered window.
    inline MmioFd *mmiofd_register(hcpp::u64 gpa, hcpp::u64 length,
                                   MmioReadFn read_fn, MmioWriteFn write_fn,
                                   void *context, bool coalesced) noexcept {
        if (length == 0 || read_fn == nullptr || write_fn == nullptr) {
            return nullptr;
        }
        const hcpp::u64 gpa_end = gpa + length;
        if (gpa_end < gpa) return nullptr;  // wraparound
        MmioFd *table = detail::mmiofd_table();
        for (hcpp::size_t i = 0; i < mmiofd_max_slots; ++i) {
            const MmioFd &other = table[i];
            if (!other.registered) continue;
            if (gpa < other.gpa_end && other.gpa < gpa_end) return nullptr;
        }
        for (hcpp::size_t i = 0; i < mmiofd_max_slots; ++i) {
            MmioFd &fd = table[i];
            if (fd.registered) continue;
            fd = MmioFd{};
            fd.gpa = gpa;
            fd.gpa_end = gpa_end;
            fd.read_fn = read_fn;
            fd.write_fn = write_fn;
            fd.context = context;
            fd.slot = static_cast<hcpp::u16>(i);
            fd.coalesced = coalesced;
            fd.registered = true;
            return &fd;
        }
        return nullptr;
    }

    inline bool mmiofd_unregister(MmioFd &fd) noexcept {
        if (!fd.registered) return false;
        fd.registered = false;
        fd.read_fn = nullptr;
        fd.write_fn = nullptr;
        fd.context = nullptr;
        return true;
    }

    inline MmioFd *mmiofd_lookup(hcpp::u64 gpa) noexcept {
        MmioFd *table = detail::mmiofd_table();
        for (hcpp::size_t i = 0; i < mmiofd_max_slots; ++i) {
            MmioFd &fd = table[i];
            if (fd.registered && gpa >= fd.gpa && gpa < fd.gpa_end) return &fd;
        }
        return nullptr;
    }

    // VM-exit router entry points: call from the EPT-violation handler
    // with the exit qualification's GPA and access width. A false return
    // means the GPA is not MMIO — escalate to a real guest fault.
    inline bool mmiofd_route_read(hcpp::u64 gpa, hcpp::u8 size,
                                  hcpp::u64 &value_out) noexcept {
        MmioFd *fd = mmiofd_lookup(gpa);
        if (fd == nullptr) return false;
        value_out = fd->read_fn(fd->context, gpa, size);
        ++detail::mmiofd_read_hits();
        return true;
    }

    inline bool mmiofd_route_write(hcpp::u64 gpa, hcpp::u64 value,
                                   hcpp::u8 size) noexcept {
        MmioFd *fd = mmiofd_lookup(gpa);
        if (fd == nullptr) return false;
        fd->write_fn(fd->context, gpa, value, size);
        ++detail::mmiofd_write_hits();
        return true;
    }

    inline hcpp::u32 mmiofd_count() noexcept {
        hcpp::u32 count = 0;
        const MmioFd *table = detail::mmiofd_table();
        for (hcpp::size_t i = 0; i < mmiofd_max_slots; ++i) {
            if (table[i].registered) ++count;
        }
        return count;
    }

    inline hcpp::u32 mmiofd_routed_reads() noexcept {
        return detail::mmiofd_read_hits();
    }
    inline hcpp::u32 mmiofd_routed_writes() noexcept {
        return detail::mmiofd_write_hits();
    }

}  // namespace hype::kvm
