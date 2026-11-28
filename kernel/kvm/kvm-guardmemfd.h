#pragma once

#include "hcpp/types.hpp"

namespace hype::kvm {

    // GuardMemFd — guarded memory-fd: a guest-physical memory descriptor
    // with poisoned guard pages on both ends plus a per-page access
    // tracking bitmap. Loosely modelled on Linux KVM memslots crossed
    // with memfd semantics: a VM registers one GuardMemFd per contiguous
    // guest-physical RAM region ("slot"), and the hypervisor routes
    // guest-physical accesses through guardmemfd_lookup() to decide
    // whether an access hits payload RAM or a guard hole.
    //
    // Guard pages exist to catch out-of-bounds DMA / virtio descriptor
    // bugs: a guest access that lands in [base_phys - guard_before,
    // base_phys) or [base_phys + size, base_phys + size + guard_after)
    // is NOT backed RAM; guardmemfd_fault() is the hook point the
    // EPT-violation / #PF handler calls to decide what to do with it
    // (kill the vcpu, inject #GP, log, etc. — policy lives in the
    // registered GuardFaultHook).
    //
    // Access tracking (flags & memfd_track_access): every
    // guardmemfd_touch() sets a bit for the touched 4 KiB guest page so
    // the host can later answer "which pages did the guest use since
    // the last reset" — the primitive behind live-migration-style
    // dirty/access scans without EPT support.
    //
    // Header-only, no heap: all state lives in a fixed slot pool.

    constexpr hcpp::u32 memfd_read_only    = 1u << 0;  // guest must not write (ROM/flash)
    constexpr hcpp::u32 memfd_guard_before = 1u << 1;  // poisoned guard band below base_phys
    constexpr hcpp::u32 memfd_guard_after  = 1u << 2;  // poisoned guard band above base_phys+size
    constexpr hcpp::u32 memfd_track_access = 1u << 3;  // maintain the slot_bitmap on touch()
    constexpr hcpp::u32 memfd_executable   = 1u << 4;  // guest instruction fetches allowed
    constexpr hcpp::u32 memfd_no_guest     = 1u << 5;  // host-private region (guest access faults)

    constexpr hcpp::size_t guardmemfd_max_slots = 8;
    // Pages per slot covered by the access bitmap. 512 pages = 2 MiB of
    // tracked guest RAM per slot; touches beyond that are still served,
    // they just are not recorded.
    constexpr hcpp::size_t guardmemfd_tracked_pages = 512;
    constexpr hcpp::u64 guardmemfd_page_size = 4096;

    struct GuardMemFd {
        hcpp::u64 base_phys;   // first guest-physical byte this slot covers
        hcpp::u64 size;        // bytes of usable guest RAM in the slot
        hcpp::u32 flags;       // memfd_* above
        hcpp::u32 guard_size;  // bytes of guard band on each enabled end
        hcpp::u64 host_virt;   // host VA of the backing store (0 = unbacked slot)
        hcpp::u16 slot_id;     // index in the VM slot table (KVM "slot" number)
        hcpp::u16 generation;  // bumped on unregister so stale lookups mismatch
        hcpp::u64 touches;     // total guardmemfd_touch() calls on this slot
        bool      used;        // pool entry is live
        bool      registered;  // visible to guardmemfd_lookup()/faults
        // One bit per tracked guest page: bit i == page (base_phys +
        // i*4096) has been touched since the last guardmemfd_clear_access().
        hcpp::u8  slot_bitmap[guardmemfd_tracked_pages / 8];
    };

    // Policy for what happens when a guest-physical access lands on a
    // guard band or a memfd_no_guest region. `gpa` is the faulting
    // address, `slot` the GuardMemFd whose guard caught it (may be a
    // nullptr context if the fault is "no slot at all"), `write` is the
    // access direction. Return true if the hook fully handled the fault
    // (vcpu continues), false to let the caller escalate.
    using GuardFaultHook = bool (*)(const GuardMemFd &slot, hcpp::u64 gpa,
                                    bool write) noexcept;

    namespace detail {
        inline GuardMemFd *guardmemfd_pool() noexcept {
            static GuardMemFd pool[guardmemfd_max_slots]{};
            return pool;
        }
        inline hcpp::u16 &guardmemfd_generation() noexcept {
            static hcpp::u16 generation = 1;
            return generation;
        }
        inline GuardFaultHook &guardmemfd_hook() noexcept {
            static GuardFaultHook hook = nullptr;
            return hook;
        }
    }  // namespace detail

    // Allocates a GuardMemFd from the fixed pool. The slot is NOT yet
    // visible to lookups — call guardmemfd_register() once backing and
    // flags are finalized. Returns nullptr when the pool is exhausted.
    inline GuardMemFd *guardmemfd_alloc(hcpp::u64 base_phys, hcpp::u64 size,
                                        hcpp::u32 flags, hcpp::u32 guard_size) noexcept {
        for (hcpp::size_t i = 0; i < guardmemfd_max_slots; ++i) {
            GuardMemFd &fd = detail::guardmemfd_pool()[i];
            if (fd.used) continue;
            fd = GuardMemFd{};
            fd.base_phys = base_phys;
            fd.size = size;
            fd.flags = flags;
            fd.guard_size = guard_size;
            fd.slot_id = static_cast<hcpp::u16>(i);
            fd.generation = detail::guardmemfd_generation();
            fd.used = true;
            return &fd;
        }
        return nullptr;
    }

    // Publishes a GuardMemFd into the active slot table. Fails on a
    // payload overlap with an already-registered slot — guard bands are
    // allowed to overlap (they only fault, they are not backed RAM).
    inline bool guardmemfd_register(GuardMemFd &fd) noexcept {
        if (!fd.used || fd.registered || fd.size == 0) return false;
        GuardMemFd *pool = detail::guardmemfd_pool();
        for (hcpp::size_t i = 0; i < guardmemfd_max_slots; ++i) {
            const GuardMemFd &other = pool[i];
            if (!other.registered || &other == &fd) continue;
            const hcpp::u64 other_end = other.base_phys + other.size;
            const hcpp::u64 fd_end = fd.base_phys + fd.size;
            if (fd.base_phys < other_end && other.base_phys < fd_end) {
                return false;  // overlapping guest RAM slots
            }
        }
        fd.registered = true;
        return true;
    }

    // Removes a slot from the active table and releases the pool entry.
    // Lookups made before this call keep working through the caller's
    // own pointer but generation no longer matches.
    inline bool guardmemfd_unregister(GuardMemFd &fd) noexcept {
        if (!fd.used) return false;
        fd.registered = false;
        fd.used = false;
        fd.generation = 0;
        ++detail::guardmemfd_generation();
        return true;
    }

    // Finds the registered slot whose *payload* range contains gpa.
    // Guard bands are deliberately excluded: those go through
    // guardmemfd_is_guard()/guardmemfd_fault().
    inline GuardMemFd *guardmemfd_lookup(hcpp::u64 gpa) noexcept {
        GuardMemFd *pool = detail::guardmemfd_pool();
        for (hcpp::size_t i = 0; i < guardmemfd_max_slots; ++i) {
            GuardMemFd &fd = pool[i];
            if (!fd.registered) continue;
            if (gpa >= fd.base_phys && gpa < fd.base_phys + fd.size) return &fd;
        }
        return nullptr;
    }

    // True when gpa lands inside a registered slot's guard band or on a
    // memfd_no_guest payload region — i.e. an address the guest should
    // never legitimately touch.
    inline bool guardmemfd_is_guard(hcpp::u64 gpa, const GuardMemFd *&slot) noexcept {
        slot = nullptr;
        GuardMemFd *pool = detail::guardmemfd_pool();
        for (hcpp::size_t i = 0; i < guardmemfd_max_slots; ++i) {
            const GuardMemFd &fd = pool[i];
            if (!fd.registered) continue;
            if ((fd.flags & memfd_no_guest) != 0 &&
                gpa >= fd.base_phys && gpa < fd.base_phys + fd.size) {
                slot = &fd;
                return true;
            }
            const hcpp::u64 gsize = fd.guard_size;
            if ((fd.flags & memfd_guard_before) != 0 && gsize != 0 &&
                gpa >= fd.base_phys - gsize && gpa < fd.base_phys) {
                slot = &fd;
                return true;
            }
            if ((fd.flags & memfd_guard_after) != 0 && gsize != 0 &&
                gpa >= fd.base_phys + fd.size && gpa < fd.base_phys + fd.size + gsize) {
                slot = &fd;
                return true;
            }
        }
        return false;
    }

    // Records a successful guest access: marks the 4 KiB page in the
    // slot's access bitmap when memfd_track_access is set. Call this
    // from the EPT/backing-map path after an access is translated, or
    // from unit tests.
    inline void guardmemfd_touch(GuardMemFd &fd, hcpp::u64 gpa) noexcept {
        if (!fd.registered) return;
        ++fd.touches;
        if ((fd.flags & memfd_track_access) == 0) return;
        const hcpp::u64 page = (gpa - fd.base_phys) / guardmemfd_page_size;
        if (page >= guardmemfd_tracked_pages) return;
        fd.slot_bitmap[page / 8] |= static_cast<hcpp::u8>(1u << (page % 8));
    }

    // Clears the access bitmap (start of a new scan epoch).
    inline void guardmemfd_clear_access(GuardMemFd &fd) noexcept {
        for (hcpp::size_t i = 0; i < guardmemfd_tracked_pages / 8; ++i) {
            fd.slot_bitmap[i] = 0;
        }
    }

    // Counts pages touched since the last guardmemfd_clear_access().
    inline hcpp::size_t guardmemfd_accessed_pages(const GuardMemFd &fd) noexcept {
        hcpp::size_t count = 0;
        for (hcpp::size_t i = 0; i < guardmemfd_tracked_pages / 8; ++i) {
            hcpp::u8 byte = fd.slot_bitmap[i];
            while (byte != 0) {
                count += byte & 1u;
                byte = static_cast<hcpp::u8>(byte >> 1);
            }
        }
        return count;
    }

    // Installs the policy hook invoked by guardmemfd_fault(). Pass
    // nullptr to return to the default (unhandled) behaviour.
    inline void guardmemfd_set_fault_hook(GuardFaultHook hook) noexcept {
        detail::guardmemfd_hook() = hook;
    }

    // Guard-page fault hook point: the VMX exit / #PF path calls this
    // with the faulting guest-physical address. If gpa hits a guard
    // band (or a no-guest region) the installed GuardFaultHook decides
    // the outcome; without a hook the fault is reported as unhandled so
    // the caller can inject a guest #PF/#GP.
    // Returns: 0 = not a guard fault (gpa maps a normal payload or
    //            nothing at all — normal page-fault handling applies),
    //          1 = guard hit, hook handled it, vcpu resumes,
    //         -1 = guard hit, unhandled (inject fault into guest).
    inline int guardmemfd_fault(hcpp::u64 gpa, bool write) noexcept {
        const GuardMemFd *slot = nullptr;
        if (!guardmemfd_is_guard(gpa, slot)) return 0;
        const GuardFaultHook hook = detail::guardmemfd_hook();
        if (hook != nullptr && slot != nullptr && hook(*slot, gpa, write)) {
            return 1;
        }
        return -1;
    }

    inline hcpp::u32 guardmemfd_count() noexcept {
        hcpp::u32 count = 0;
        const GuardMemFd *pool = detail::guardmemfd_pool();
        for (hcpp::size_t i = 0; i < guardmemfd_max_slots; ++i) {
            if (pool[i].registered) ++count;
        }
        return count;
    }

}  // namespace hype::kvm
