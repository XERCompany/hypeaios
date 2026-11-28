#pragma once

#include "hcpp/types.hpp"

namespace hype::kvm {

    // DirtyRing — per-VM dirty-page ring, modelled on Linux KVM's
    // KVM_CAP_DIRTY_LOG_RING / PML (Page Modification Logging) path.
    //
    // While PML is enabled the CPU appends the guest-frame number (GFN)
    // of every dirtied guest-physical page to the PML buffer at every
    // EPT write that sets a dirty bit; the PML-full VM exit hands those
    // GFNs to the hypervisor, which calls dirtyring_mark() to publish
    // {gfn, slot} records for userspace/live-migration to consume via
    // dirtyring_snapshot(). This replaces the racy read-and-clear of a
    // per-slot dirty bitmap with a per-VM ordered stream.
    //
    // Indices are wraparound-safe: producer/consumer are free-running
    // u32 counters that wrap modulo 2^32; the number of pending entries
    // is (producer - consumer), and the slot index inside the ring is
    // (index % dirtyring_size). A full ring cannot store more marks —
    // overflow marks are counted in `dropped` and the caller is expected
    // to fall back to a full dirty scan (same semantics as Linux's
    // ring-full reset hint).
    //
    // Header-only, no heap; one DirtyRing is owned by each Vm.

    constexpr hcpp::size_t dirtyring_size = 1024;  // power of two

    struct DirtyRingEntry {
        hcpp::u64 gfn;    // guest frame number (guest-physical addr >> 12)
        hcpp::u16 slot;   // GuardMemFd slot_id the gfn belongs to
        hcpp::u16 flags;  // dirtyring_flag_* below
        hcpp::u32 pad;    // keep the entry 16 bytes
    };

    constexpr hcpp::u16 dirtyring_flag_none  = 0;
    constexpr hcpp::u16 dirtyring_flag_reset = 1u << 0;  // ring overflowed; full scan required

    struct DirtyRing {
        DirtyRingEntry entries[dirtyring_size];
        hcpp::u32 producer;   // next index to write (monotonic, wraps)
        hcpp::u32 consumer;   // next index to read
        hcpp::u32 dropped;    // marks lost because the ring was full
        hcpp::u32 harvests;   // PML-full exits drained into this ring
        bool      enabled;    // PML/dirty logging is active for the VM
    };

    inline void dirtyring_init(DirtyRing &ring) noexcept {
        ring = DirtyRing{};
        ring.enabled = true;
    }

    inline hcpp::u32 dirtyring_pending(const DirtyRing &ring) noexcept {
        return ring.producer - ring.consumer;  // wraparound-safe depth
    }

    inline hcpp::u32 dirtyring_free(const DirtyRing &ring) noexcept {
        return static_cast<hcpp::u32>(dirtyring_size) - dirtyring_pending(ring);
    }

    // Producer: appends {gfn, slot}. Called from the PML-full VM-exit
    // handler (one call per harvested PML entry) or from software dirty
    // emulation paths. Returns false when the ring is full; the entry
    // is counted in `dropped` and the caller should push a
    // dirtyring_flag_reset record or force a full dirty scan.
    inline bool dirtyring_mark(DirtyRing &ring, hcpp::u64 gfn,
                               hcpp::u16 slot) noexcept {
        if (!ring.enabled) return false;
        if (dirtyring_free(ring) == 0) {
            ++ring.dropped;
            return false;
        }
        DirtyRingEntry &entry = ring.entries[ring.producer % dirtyring_size];
        entry.gfn = gfn;
        entry.slot = slot;
        entry.flags = dirtyring_flag_none;
        entry.pad = 0;
        ++ring.producer;
        return true;
    }

    // Consumer: drains up to `max` entries into `out`, advancing the
    // consumer index. Returns the number of entries copied — the
    // userspace dirty-sync loop keeps calling until it returns 0.
    inline hcpp::size_t dirtyring_snapshot(DirtyRing &ring, DirtyRingEntry *out,
                                           hcpp::size_t max) noexcept {
        hcpp::size_t copied = 0;
        while (copied < max && dirtyring_pending(ring) != 0) {
            out[copied++] = ring.entries[ring.consumer % dirtyring_size];
            ++ring.consumer;
        }
        return copied;
    }

    // Fast-forwards the consumer past every pending entry without
    // reading them (start of a new dirty-log epoch).
    inline void dirtyring_reset(DirtyRing &ring) noexcept {
        ring.consumer = ring.producer;
    }

}  // namespace hype::kvm
